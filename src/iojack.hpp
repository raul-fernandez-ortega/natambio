/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#ifndef _NA_IOJACK_HPP_
#define _NA_IOJACK_HPP_

extern "C" {

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

#include <jack/jack.h>
#include <jack/thread.h>
#include <jack/statistics.h>

}

#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <ctime>

#include "structs.hpp"
#include "cycletime.hpp"
#include "convchannel.hpp"
#include "nae.hpp"

/* Bounds on a port gain, in dB. They exist because the remote manager adds to
   whatever the gain already is: a key bound to "up 1.0" and held down would
   otherwise walk a tweeter up to whatever the caller had the patience for. */
#define NA_PORT_GAIN_MAX_DB    20.0
#define NA_PORT_GAIN_MIN_DB  -120.0

/* Where mute puts the outputs. The same floor as a gain trimmed all the way
   down, which is silence in any arithmetic that matters here. */
#define NA_MUTE_DB           -120.0

struct jack_port {
  jack_port_t* port;
  string port_name;
  /* The port's <gain>. gain_db is the value the configuration and the remote
     manager work in and belongs to them; gain_target is that same number,
     linear, and is the only one of the three the RT callback reads from
     outside; gain is where the callback has got to, slewed towards the target
     so that a gain arriving mid-stream is heard as a short fade and not as a
     click. One writer each: gain_target from outside, gain from the callback. */
  double gain_db;
  volatile float gain_target;
  float gain;
  /* Inputs only: JACK's input buffer is shared with every other client reading
     the same port and must not be scaled in place, so the scaled copy goes
     here. Sized at registration for EVERY input port, whatever its gain: the
     remote manager can take a port off unity at any time and the callback is
     in no position to allocate. */
  vector<float> gain_buf;
  vector<ConvChannel*> channels;
  vector<struct nae_channel*> nae_channels;
};

/* An NAE engine's configuration, read back out: the fields a <nae> tag carries
   in the XML, so that an engine tuned by ear can be written down as the block
   that would reproduce it. Gains are in dB, as the file spells them and as the
   remote manager works in -- not the linear numbers naconf parses them into.
   The output names are empty for the outputs this engine does not have. */
struct nae_config {
  string name;
  /* The tag the engine is declared with, "nae" or "nae_erb". The three fields
     after the gains mean something only for the second, and are what a report
     has to add to the block for it to start another natambio unchanged. */
  string engine;
  double erb_cov_window_ms;
  double erb_delta_erb;
  double erb_band_min_hz;
  int mode;                     /* 0 alpha, 1 beta */
  int steps_length;
  double pan_scale;
  double front_gain_db;
  double ambience_gain_db;
  double rear_gain_db;
  string input_left, input_right;
  string output_left, output_right;
  string front_output_left, front_output_right;
  string amb_output_left, amb_output_right;
};

class ioJack {

private:

  int sample_format;
  int sample_rate;
  int fragment_size;
  int priority;
  int policy;

  bool quiet;
  bool has_started;
  jack_client_t *jackclient;
  char* client_name;
  vector<ConvChannel*> conv_channels;
  vector<NAE*> nae_channels;
  vector<struct jack_port*> jack_inputs;
  vector<struct jack_port*> jack_outputs;
  Convproc *convproc;

  /* Output ramp. Every output buffer is scaled by a gain the callback slews
     towards ramp_target, interpolated sample by sample, so neither the first
     period after jack_activate() nor the last one before jack_deactivate()
     steps the signal: a step is a click, and through a subwoofer a thump.
     ramp_target is written by the main thread and ramp_gain read back by it
     (fadeOut()); both are single words touched by exactly one writer. */
  volatile float ramp_target;   /* 1.0 running, 0.0 muted -- main thread writes */
  volatile float ramp_gain;     /* current gain -- RT thread writes */
  float ramp_inc;               /* per-sample slew, set from the sample rate */

  /* Where the period goes. Two timers written by the RT callback and read by
     the manager thread (cycletime.hpp): cycle_time is the callback end to end,
     conv_time the convolution stage alone -- the input sums, zita-convolver's
     own process(), and the delay/scale/mix on the way out, which the callback
     runs in two pieces with the output ports filled in between, and which the
     timer adds back into one figure per cycle. What is left between the two is
     the port plumbing and the gain ramps, and it is left rather than timed:
     the number worth having there is the total, and a stage that costs
     nanoseconds does not need its own history. The NAE engines time
     themselves, each in its own thread (NAE::timeStats()).
     They cost four clock_gettime() per period between them -- vDSO reads, a
     few tens of nanoseconds each -- against a period of a thousand
     microseconds or more, so they are always on: a load figure that has to be
     switched on first is one nobody has when the xruns start. */
  CycleTimer cycle_time;
  CycleTimer conv_time;

  /* The xruns, which are the other half of the same question: the timers above
     say how close the periods that survived came, and these say how many did
     not. Two figures rather than one. xrun_total is every xrun since the
     process started and is never cleared -- a "timecycle reset" taken to start
     a measurement would otherwise throw away the one number in the report that
     is worth keeping across one, and an xrun that nobody counted is an xrun
     nobody knows happened. xrun_time is the delay JACK reports with each of
     them, in the same FIFO every other stage uses, so the report can say how
     late the machine was and not only how often. Written by the xrun callback,
     which JACK calls from the process thread: an increment and the same two
     relaxed stores as any other timer. */
  std::atomic<unsigned long long> xrun_total;
  CycleTimer xrun_time;

  /* Mute: one word, written by the manager thread and read by the RT callback,
     the same single-writer discipline as ramp_target. Both directions are
     slewed like any other gain change -- cutting or restoring a signal
     mid-waveform is a discontinuity the size of whatever sample was playing,
     which is a click either way round. */
  volatile int mute_target;

  struct jack_port *findPort(string port_name);
  NAE *findNae(string nae_name);

  /* One period of slew towards a port's gain target: at most ramp_inc per
     sample, the rate the start/stop fade already uses, so a change lands in a
     few tens of milliseconds and no faster than the ear forgives. Returns
     where the gain gets to by the end of this period. */
  float slewGain(float current, float target) {
    float span = ramp_inc * (float)fragment_size;
    if(target > current) {
      current += span;
      if(current > target) current = target;
    } else if(target < current) {
      current -= span;
      if(current < target) current = target;
    }
    return current;
  }

public:
  
  ioJack(string clientName, bool n_quiet);
  
  bool global_init(void);
  bool addInputPort(string port_name, double gain_db = 0.0);
  bool addOutputPort(string port_name, double gain_db = 0.0);
  bool ConnectInputPort(string port_name, string dest_name);
  bool ConnectOutputPort(string port_name, string dest_name);
  void addConvChannel(ConvChannel* conv_channel);
  void addNaeChannel(NAE* n_nae);
  bool connectInputConvPort(string port_name, ConvChannel* channel);
  bool connectOutputConvPort(string port_name, ConvChannel* channel);
  bool connectInputNaePort(enum side n_side, string port_name, NAE* n_pan);
  bool connectOutputNaePort(enum side n_side, string port_name, NAE* n_pan);
  void setconvproc(Convproc * nconvproc) { convproc = nconvproc; };

  ~ioJack(void); 

  static void latency_callback(jack_latency_callback_mode_t mode, void *arg);
  void na_latency_callback(jack_latency_callback_mode_t mode);
  static int xrun_callback(void *arg);
  void na_xrun_callback(void);
  static void jack_shutdown_callback(void *arg);
  static void error_callback(const char *msg);
  void na_shutdown_callback(void);
  static int jack_process_callback(jack_nframes_t n_frames, void* arg);
  int na_process_callback(jack_nframes_t n_frames);
  
  int synch_start(void);
  void synch_stop(void);

  /* Ramp the outputs down to silence and wait for the RT callback to get there,
     up to timeout_ms (it returns at once if no callback is running, e.g. the
     server is gone). synch_stop() calls it, so every path that closes the
     client fades out first. */
  void fadeOut(int timeout_ms = 200);

  // Both return JACK's own status: 0 on success, EEXIST if the connection is
  // already made (already broken, for disconnect), any other non-zero on failure.
  int connect_port(string port_name, string dest_name);
  int disconnect_port(string port_name, string dest_name);

  /* Port gains for the remote manager (remote.cpp). Both run in the manager
     thread, never in the RT callback, and both return false if no port of that
     name is registered -- input and output ports are searched alike, as JACK
     names are unique within the client either way. adjustPortGain() adds
     delta_db to whatever the gain currently is, clamps the result to
     [NA_PORT_GAIN_MIN_DB, NA_PORT_GAIN_MAX_DB] and reports it in new_gain_db;
     the callback slews to it rather than stepping. */
  /* Mute takes the OUTPUT ports to NA_MUTE_DB, whatever their own gains, and
     leaves those gains alone: they are what the ports go back to, and what
     portGain() keeps reporting meanwhile. The inputs are deliberately left
     running -- silencing them too would add nothing audible and would drain
     the convolution tails and the NAE's window into silence, so that unmuting
     would rebuild them over a filter length instead of coming straight back. */
  void setMute(bool on);
  bool isMuted(void) { return mute_target != 0; };

  bool portGain(string port_name, double *gain_db);
  bool adjustPortGain(string port_name, double delta_db, double *new_gain_db);
  /* Registered port names, each group in the order the configuration declared
     it: what a bare "get" reports, and what the commands that address a whole
     direction at once (upin/downin, upout/downout) operate on. */
  vector<string> inputPortNames(void);
  vector<string> outputPortNames(void);
  vector<string> portNames(void);   /* inputs first, then outputs */

  /* The NAE engines' gains, for the remote manager, in the same shape as the
     port gains above and from the same thread. Both return false if no engine
     of that name was configured; a NAE with no <name> is unaddressable and is
     simply never matched. Unlike the port commands these are ABSOLUTE -- the
     three gains are a balance between components rather than a volume, and the
     number the caller has in mind is the one in the configuration file.
     *active comes back false for a gain the engine's mode does not read: the
     value is still set and still reported, it just multiplies nothing until
     the configuration says otherwise, and the caller is told so rather than
     left to wonder why nothing moved. The engine slews to it, so the change is
     a fade like any other. */
  vector<string> naeNames(void);
  /* The whole configuration of one engine, by name or by position. The second
     form exists because an engine configured without a <name> cannot be
     addressed by one and would otherwise be missing from a report of all of
     them -- which is the one report where its absence would go unnoticed. */
  size_t naeCount(void) { return nae_channels.size(); };
  bool naeConfig(string nae_name, struct nae_config *cfg);
  bool naeConfigAt(size_t index, struct nae_config *cfg);
  /* An engine's <pan_scale>, read and written. setNaePanScale() returns false
     for a name that is not registered AND for a width outside [-1, 1], which
     the engine refuses rather than clamps; the manager checks the range first
     so that it can say which of the two went wrong. The engine slews to it,
     like a gain. */
  bool naePanScale(string nae_name, double *scale);
  bool setNaePanScale(string nae_name, double scale, double *new_scale);
  bool naeGain(string nae_name, enum nae_gain which, double *gain_db, bool *active);
  bool setNaeGain(string nae_name, enum nae_gain which, double db,
                  double *new_gain_db, bool *active);

  /* Per-cycle times for the remote manager ("timecycle"), all four from its
     thread and none from the RT one. The figures are microseconds; what they
     are computed over, and what the reader races with, is cycletime.hpp.
     The engines are addressed by position and not by name, as naeConfigAt()
     is and for the same reason: an engine configured without a <name> is part
     of what the period has to fit in, and a report of where the time goes is
     the last place one should be missing from. naeTimeStatsAt() returns false
     past the end of the list and reports the engine's name, empty for an
     unnamed one. resetTimeStats() clears the callback's two timers, every
     engine's and the xrun delays, so that a measurement starts where the
     caller asked it to and not where the last one left off; the count of the
     xruns is the one thing it does not clear. */
  void cycleTimeStats(struct na_time_stats *st) { cycle_time.stats(st); };
  void convTimeStats(struct na_time_stats *st) { conv_time.stats(st); };
  bool naeTimeStatsAt(size_t index, struct na_time_stats *st, string *name);
  void resetTimeStats(void);

  /* The xruns: how many since the process started, and the delays JACK
     reported with the last thousand of them. The count survives
     resetTimeStats(); the delays do not, being a window like every other one
     here. The delay figures are only as good as the backend, which is free to
     report zero, and a zero is stored as the zero it was measured as rather
     than dropped -- a row of zeroes says the backend does not report delays,
     which is worth knowing, and a dropped sample would say nothing at all. */
  unsigned long long xrunCount(void) { return xrun_total.load(std::memory_order_relaxed); };
  void xrunTimeStats(struct na_time_stats *st) { xrun_time.stats(st); };

  const char **get_jack_port_connections(string port_name);
  const char **get_jack_ports(void);
  const char **get_jack_input_physical_ports(void);
  const char **get_jack_input_ports(void);
  const char **get_jack_output_physical_ports(void);
  const char **get_jack_output_ports(void);
  int getSampleRate(void) { return sample_rate; };
  bool is_running(void) { return has_started; };
  int getPartSize(void) { return fragment_size; };
  int getPriority(void) { return priority; };
   int getPolicy(void) { return policy; };

};

#endif
