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

struct jack_port {
  jack_port_t* port;
  string port_name;
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

public:
  
  ioJack(string clientName, bool n_quiet);
  
  bool global_init(void);
  bool addInputPort(string port_name);
  bool addOutputPort(string port_name);
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
