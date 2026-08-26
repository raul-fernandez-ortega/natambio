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
#include "convchannel.hpp"
#include "nae.hpp"

/* Bounds on a port gain, in dB. They exist because the remote manager adds to
   whatever the gain already is: a key bound to "up 1.0" and held down would
   otherwise walk a tweeter up to whatever the caller had the patience for. */
#define NA_PORT_GAIN_MAX_DB    20.0
#define NA_PORT_GAIN_MIN_DB  -120.0

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

  struct jack_port *findPort(string port_name);

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
  bool portGain(string port_name, double *gain_db);
  bool adjustPortGain(string port_name, double delta_db, double *new_gain_db);
  /* Every registered port, inputs first and then outputs, each in the order it
     was declared in the configuration. What a bare "get" reports. */
  vector<string> portNames(void);

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
