/*
 * (c) Copyright 2024/2025 -- Raul Fernandez Ortega
 *
 * This program is open source. For license terms, see the LICENSE file.
 *
 */

#ifndef _CONVCHANNEL_HPP_
#define _CONVCHANNEL_HPP_


#ifdef __cplusplus
extern "C" {
#endif
  
#include <jack/jack.h>
#include <zita-convolver.h>

#ifdef __cplusplus
}
#endif

#include "structs.hpp"
#include "nae.hpp"

#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace std;

struct nae_channel {
  enum side n_side;
  NAE* n_nae;
};

struct iobuffer {
  string port_name;
  float* buffer;
};

/* Convolver channels controler (input/ouput). Each convolver has one input channel and one output channel:
   Input channel can ben connected to several jackaudio input sources (jack_inp vector). Input signal would be the mix of every input source.
   Output channel can be connected to several jackaudio output sinks (jack_out vector). Each output signal would be a copy on ConvChannel output
   Input channel can be connected to several ConvChannels outputs (given by index ---> o_proc_inp vector of ConvChannels indexes)
   Input channel can be connected to several nae processes output (vector of nae_channel structs)
   string name: ConvChannel name for configuration identification
   index: zita-convolver buffer index (input and output) for this ConvChannel
   ptsize: convolver partition size (equal to jackaudio frames/period)
   delay: output delay in samples
   scale: output gain (linear value)
   bypass: if true there is no zita-convolver process. Input ---> Output (+delay, *scale)
   p_bypass, outbut, delaybuf: auxiliary buffers for output processing
   convproc: pointer to Convproc object (zita-convolver library) controller of all convolution process. There is only on convproc common to all ConvChannels
*/
class ConvChannel {

private:

  string name;
  bool quiet;
  int index;
  unsigned int ptsize;
  int delay;
  float scale;
  Convproc *convproc;
  float* outbuf;
  float* p_bypass;
  float* p_out;
  bool bypass;
  vector<struct iobuffer*> jack_inp;
  vector<struct iobuffer*> jack_out;
  vector<ConvChannel*> o_conv_inp;
  vector<struct nae_channel*> o_nae_inp;

public:

  ConvChannel(Convproc* n_convproc, unsigned int n_ptsize, string n_name, int n_index, bool n_quiet);
  ~ConvChannel(void);

  void set_bypass(bool bps) { bypass = bps; }; 
  void set_delay(int dl);
  int get_delay(void) { return delay; };
  void set_scale(float n_scale) { scale = n_scale; };
  float get_scale(void) { return scale; };
  int get_index(void) { return index; };
  void set_name(string n_name) { name = n_name; };
  string get_name(void) { return name; };
  vector<struct iobuffer*> get_jack_inp(void) { return jack_inp; };
  vector<struct iobuffer*> get_jack_out(void) { return jack_out; };
  vector<struct nae_channel*> get_o_nae_inp(void) { return o_nae_inp; };
  void addInputBuffer(string port_name);
  void addOutputBuffer(string port_name);
  void addBypassBuffer(int size);
  void addConvProcBuffer(ConvChannel *conv_cn) { o_conv_inp.push_back(conv_cn); };
  void addNaeInput(enum side n_side, NAE *n_nae);
  void fillInputBuffer(string port_name, float* buffer);
  void fillOutputBuffer(string port_name, float* buffer);
  void copyOutputBuffer(float* buffer);
  void processInput(int bufsize);
  void processOutput(int bufsize);

};

#endif

