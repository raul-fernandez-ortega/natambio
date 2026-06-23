/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */


#include "convchannel.hpp"

ConvChannel::ConvChannel(Convproc* n_convproc, unsigned int n_ptsize, string n_name, int n_index, bool n_quiet):
  name(n_name),
  quiet(n_quiet),
  index(n_index),
  ptsize(n_ptsize),
  delay(0),
  scale(1),
  convproc(n_convproc),
  outbuf(NULL),
  p_bypass(NULL),
  p_out(NULL)

{
    if(!quiet)
    std::cout << "ConvChannel: new ConvChannel with convproc index: " << n_index << std::endl;
}

ConvChannel::~ConvChannel(void)
{
  if(p_out)
    free(p_out);
  if(outbuf)
    free(outbuf);
  if(p_bypass)
    free(p_bypass);
  for (std::vector<iobuffer*>::iterator it = jack_inp.begin() ; it != jack_inp.end(); ++it)
    delete *it;
  jack_inp.clear();
  for (std::vector<iobuffer*>::iterator it = jack_out.begin() ; it != jack_out.end(); ++it)
    delete *it;
  jack_out.clear();
  for (std::vector<struct nae_channel*>::iterator it = o_nae_inp.begin() ; it != o_nae_inp.end(); ++it)
    delete *it; 
  o_nae_inp.clear();
}

void ConvChannel::set_delay(int dl)
{
  if(!quiet)
    std::cout << "ConvChannel: setting delay to " << dl << " samples" << std::endl;
  delay = dl;
  p_out = (float*) malloc(ptsize*sizeof(float));
  outbuf = (float*) malloc((ptsize + delay)*sizeof(float));
  memset(p_out, 0, ptsize *sizeof(float));
  memset(outbuf, 0, (ptsize + delay) * sizeof(float));
}

void ConvChannel::addInputBuffer(string port_name)
{
#ifdef RTDEBUG
  cout << "ConvChannel: add input buffer to convolver index " << index << " connected to jack port name " << port_name << endl;
#endif
  iobuffer *inputbuf = new struct iobuffer;
  inputbuf->port_name = port_name;
  inputbuf->buffer = NULL;
  jack_inp.push_back(inputbuf);
}

void ConvChannel::addOutputBuffer(string port_name)
{
#ifdef RTDEBUG
  std::cout << "ConvChannel: add output buffer to convolver index " << index << " connected to jack port name " << port_name << std::endl;
#endif
  iobuffer *outputbuf = new struct iobuffer;
  outputbuf->port_name = port_name;
  outputbuf->buffer = NULL;
  jack_out.push_back(outputbuf);
}

void ConvChannel::addBypassBuffer(int size)
{
#ifdef RTDEBUG
  cout << "ConvChannel: add bypass buffer to convolver index " << index << std::endl;
#endif
  p_bypass = (float*) malloc(size*sizeof(float));
}

void ConvChannel::addNaeInput(enum side n_side, NAE *n_nae)
{
#ifdef RTDEBUG
  if(n_side == LEFT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(LEFT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
  else if (n_side == RIGHT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(RIGHT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
  else   if(n_side == MID_LEFT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(MID_LEFT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
  else if (n_side == MID_RIGHT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(MID_RIGHT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
  else   if(n_side == SIDE_LEFT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(SIDE_LEFT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
  else if (n_side == SIDE_RIGHT)
    cout << "ConvChannel: add nae output ->" << n_nae->getName() << "--" << n_nae->getChannelOut(SIDE_RIGHT) << "/" << n_side << " connection to convolver index ->" << index << std::endl;
#endif
  nae_channel *n_nae_channel = new struct nae_channel();
  n_nae_channel->n_side = n_side;
  n_nae_channel->n_nae = n_nae;
  o_nae_inp.push_back(n_nae_channel);
}

void ConvChannel::fillInputBuffer(string port_name, float* buffer)
{
  for (std::vector<iobuffer*>::iterator it = jack_inp.begin() ; it != jack_inp.end(); ++it) 
    if((*it)->port_name == port_name) {
#ifdef RTDEBUG
	std::cout << "ConvChannel: copying input buffer for convolver " << index << " connected to jack port name " << port_name << std::endl;
#endif
      (*it)->buffer = buffer;
    }
}

void ConvChannel::fillOutputBuffer(string port_name, float* buffer)
{
  for (std::vector<iobuffer*>::iterator it = jack_out.begin() ; it != jack_out.end(); ++it) 
    if((*it)->port_name == port_name) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: copying output buffer from convolver " << index << " connected to jack port name " << port_name << std::endl;
#endif
      (*it)->buffer = buffer;
    }
}

void ConvChannel::copyOutputBuffer(float* buffer)
{
  float *p_conv;
  if(bypass) {
    for(unsigned int i = 0; i < ptsize; i++)
      buffer[i] += p_bypass[i];
  } else {
    p_conv = convproc->outdata(index);
    for(unsigned int i = 0; i < ptsize; i++)
      buffer[i] += p_conv[i];
  }
}

void ConvChannel::processInput(int bufsize)
{
  float *p_in;

  // Bypass process. Copying jack_connection inputs to bypass buffer then to output
  if(bypass) {
#ifdef RTDEBUG
    std::cout << "ConvChannel: bypass processing input " << std::endl;
#endif
    memset(p_bypass, 0, bufsize * sizeof(float));
    // Bypass jack inputs
    for (vector<struct iobuffer*>::iterator inbuffer = jack_inp.begin() ; inbuffer != jack_inp.end(); inbuffer++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from jack port " << (*inbuffer)->port_name << " to bypass " << index << std::endl;
#endif
      for(int i = 0; i < bufsize; i++)
	p_bypass[i] += (*inbuffer)->buffer[i];
    }
    // Bypass other convolver process outputs 
    for (vector<ConvChannel*>::iterator o_conv_i = o_conv_inp.begin() ; o_conv_i != o_conv_inp.end(); o_conv_i++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from convproc " << (*o_conv_i)->index << " to bypass " << index << std::endl;
#endif
      (*o_conv_i)->copyOutputBuffer(p_bypass);
    }
    // Bypass nae inputs
    for (vector<struct nae_channel*>::iterator o_nae_p = o_nae_inp.begin() ; o_nae_p != o_nae_inp.end(); o_nae_p++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from nae " << (*o_nae_p)->n_nae->getName() << "/" << (*o_nae_p)->n_nae->getChannelOut((*o_nae_p)->n_side) << " output to bypass " << index << std::endl;
#endif
      (*o_nae_p)->n_nae->fillOutputBuffer((*o_nae_p)->n_side, p_bypass);
    }
  }
  else {
    // Processing ConvChannel input as a sum of inputs from jack inputs, other convolver outputs and nae outputs
#ifdef RTDEBUG
    std::cout << "ConvChannel: processing input for convprov index " << index << std::endl;
#endif
    p_in = convproc->inpdata(index);
    memset(p_in, 0, bufsize * sizeof(float));
    
    //Process convproc input as a sum of jack connection inputs and convproc outputs
    for (vector<struct iobuffer*>::iterator inbuffer = jack_inp.begin() ; inbuffer != jack_inp.end(); inbuffer++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from jack port " << (*inbuffer)->port_name << " to convprov " << index << std::endl;
#endif
      for(int i = 0; i < bufsize; i++)
	p_in[i] += (*inbuffer)->buffer[i];
    }
     //Process other convproc outputs to convproc input
    for (vector<ConvChannel*>::iterator o_conv_i = o_conv_inp.begin() ; o_conv_i != o_conv_inp.end(); o_conv_i++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from convproc " << (*o_proc_i) << " to convprov " << index << std::endl;
#endif
      (*o_conv_i)->copyOutputBuffer(p_in);
    }
    
    // Processing nae outputs to convproc input
    for (vector<struct nae_channel*>::iterator o_nae_p = o_nae_inp.begin() ; o_nae_p != o_nae_inp.end(); o_nae_p++) {
#ifdef RTDEBUG
      std::cout << "ConvChannel: processing input from nae " << (*o_nae_p)->n_nae->getName() << "/" << (*o_nae_p)->n_nae->getChannelOut((*o_nae_p)->n_side) << " output to convprov " << index << std::endl;
#endif
      (*o_nae_p)->n_nae->fillOutputBuffer((*o_nae_p)->n_side, p_in);
    }
  }
}

void ConvChannel::processOutput(int bufsize)
{
  //float *p_out = (float*) malloc(bufsize*sizeof(float));

  if(bypass) {
    // Bypass process copying bypass buffer to output buffer
#ifdef RTDEBUG
    std::cout << "ConvChannel " << index << ": bypass processing output " << std::endl;
#endif
    memcpy(p_out, p_bypass, bufsize * sizeof(float));
  }
  else {
    // Processing output delay and copy to jack output buffer
#ifdef RTDEBUG
    std::cout << "ConvChannel " << index << ": processing output from convprox index " << index << std::endl;
#endif
    memcpy(p_out, convproc->outdata(index), bufsize * sizeof(float));
  }
#ifdef RTDEBUG
  std::cout << "ConvChannel " << index << ": delay is " << delay << " samples" << std::endl;
#endif
  memmove(outbuf, outbuf + bufsize, delay * sizeof(float));

  for(int i = 0; i < bufsize; i++)
    outbuf[i + delay] = p_out[i]*scale;
  
  for (vector<struct iobuffer*>::iterator outbuffer = jack_out.begin() ; outbuffer != jack_out.end(); outbuffer++) {
#ifdef RTDEBUG
    std::cout << "ConvChannel " << index << ": processing output to " << (*outbuffer)->port_name << std::endl;
#endif   
    for(int i = 0; i < bufsize; i++) {
      (*outbuffer)->buffer[i] += outbuf[i]; 
    }
  }
}

