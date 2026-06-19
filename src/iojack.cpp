/*
 * (c) Copyright 2024/2025 -- Raul Fernandez Ortega
 *
 * This program is open source. For license terms, see the LICENSE file.
 *
 */

#include "iojack.hpp"

#define DEFAULT_CLIENTNAME "natambio"

ioJack::ioJack(string clientName, bool n_quiet) 
{
  quiet = n_quiet;
  has_started = false;
  client_name = strdup(clientName.c_str());
  jackclient = NULL;
  if (client_name == NULL) 
    client_name = strdup(DEFAULT_CLIENTNAME);
  if(!quiet)
    std::cout << "iojack: new Jackaudio client: " << client_name << std::endl;
}

ioJack::~ioJack(void)
{
  for (vector<jack_port*>::iterator jack_p = jack_inputs.begin() ; jack_p != jack_inputs.end(); jack_p++) {
    for(vector<nae_channel*>::iterator pchn_p = (*jack_p)->nae_channels.begin(); pchn_p != (*jack_p)->nae_channels.end(); pchn_p++)
      delete *pchn_p;
    delete *jack_p;
  }
  jack_inputs.clear();
  for (vector<jack_port*>::iterator jack_p = jack_outputs.begin() ; jack_p != jack_outputs.end(); jack_p++) {
    for(vector<nae_channel*>::iterator pchn_p = (*jack_p)->nae_channels.begin(); pchn_p != (*jack_p)->nae_channels.end(); pchn_p++)
      delete *pchn_p;
    delete *jack_p;
  }
  jack_outputs.clear();
  for (vector<NAE*>::iterator nae_p = nae_channels.begin() ; nae_p != nae_channels.end(); nae_p++)
    delete *nae_p;
  nae_channels.clear();
  if(!quiet)
    std::cout << "iojack: closing Jackaudio client: " << client_name << std::endl;
  free(client_name);
  synch_stop();
}

void ioJack::latency_callback(jack_latency_callback_mode_t mode, void *arg)
{
  ioJack *CallbackJackObject;
  CallbackJackObject = (ioJack*) arg;
  CallbackJackObject->na_latency_callback(mode);
}

void ioJack::na_latency_callback(jack_latency_callback_mode_t mode)
{ 
  // same latency for all ports, regardless of how they are connected
  if (mode == JackPlaybackLatency) {
    // do nothing
  } else if (mode == JackCaptureLatency) {
    // to do
  }
}

int ioJack::xrun_callback(void *arg)
{
  reinterpret_cast<ioJack *>(arg)->na_xrun_callback();
  return 0;
}

void ioJack::na_xrun_callback(void)
{
  time_t timestamp;
  time(&timestamp);
  float xdelay = jack_get_xrun_delayed_usecs(this->jackclient)/1000.0f;
  std::cerr << ctime(&timestamp) << "\t\t XRUN detected with " << xdelay << " ms delay\n";
}

void ioJack::jack_shutdown_callback(void *arg)
{
  reinterpret_cast<ioJack *>(arg)->na_shutdown_callback();
}

void ioJack::na_shutdown_callback(void)
{
  throw std::runtime_error("JACK I/O: JACK daemon shut down.\n");
  has_started = false;
}

int ioJack::jack_process_callback(jack_nframes_t n_frames, void *arg)
{
  return reinterpret_cast<ioJack *>(arg)->na_process_callback(n_frames);
}

void ioJack::error_callback(const char* msg)
{
  string fmsg = "JACK I/O: JACK reported an error: %s";
  fmsg += msg;
  throw std::runtime_error(fmsg);
}

int ioJack::na_process_callback(jack_nframes_t n_frames)
{
  float *inpbuf, *outbuf;

#ifdef RTDEBUG 
  std::cout << "ioJack callback: callback calling..." << std::endl;
#endif
  // NOTE: do not call convproc->check_stop() here. It has a side effect (it sets
  // the convproc to ST_STOP whenever all convolution levels are idle, which is
  // the normal case for filters small enough to need no background worker
  // threads) and would halt the convolver after the first callback.

  // Procesing jackaudio inputs
  for (vector<jack_port*>::iterator jack_p = jack_inputs.begin() ; jack_p != jack_inputs.end(); jack_p++) {
#ifdef RTDEBUG
    std::cout << "ioJack callback: processing input for: " << (*jack_p)->port_name << std::endl;
#endif
    inpbuf = (float *)jack_port_get_buffer((*jack_p)->port, fragment_size);
    
    // Check for convChannels input connected to this jackaudio input
    for(vector<ConvChannel*>::iterator channel_p = (*jack_p)->channels.begin() ; channel_p != (*jack_p)->channels.end(); channel_p++) {
#ifdef RTDEBUG
	std::cout << "ioJack callback: passing input data to  convolver: " << (*channel_p)->get_index() << std::endl;
#endif	
      (*channel_p)->fillInputBuffer((*jack_p)->port_name, inpbuf);
    }
    
    // Check for NAE input channels conected to this jackaudio input
    for(vector<nae_channel*>::iterator nae_ch = (*jack_p)->nae_channels.begin() ; nae_ch != (*jack_p)->nae_channels.end(); nae_ch++) {
#ifdef RTDEBUG
	std::cout << "ioJack callback: passing input data to NAE: " << (*nae_ch)->n_nae->getName() << std::endl;
#endif	
	(*nae_ch)->n_nae->fillInputBuffer((*nae_ch)->n_side, inpbuf);
    }
  }
  
  // Processing convChannels input (processInput)
  for (vector<ConvChannel*>::iterator channel_p = conv_channels.begin() ; channel_p != conv_channels.end(); channel_p++) {
#ifdef RTDEBUG
      std::cout << "ioJack callback: processing input convolver: " << (*channel_p)->get_index() << std::endl;
#endif
    (*channel_p)->processInput(fragment_size);
  }
  
  // Running convolutions (convproc threads)
#ifdef RTDEBUG
  std::cout << "ioJack callback: convproc processing..." << std::endl;
#endif
  if(!conv_channels.empty())
    convproc->process(true);

#ifdef RTDEBUG
  std::cout << "ioJack callback: convproc processing finished..." << std::endl;
#endif

  // Processing jackaudio outputs
  for (vector<jack_port*>::iterator jack_p = jack_outputs.begin() ; jack_p != jack_outputs.end(); jack_p++) {
#ifdef RTDEBUG
    std::cout << "ioJack callback: processing output for: " << (*jack_p)->port_name << std::endl;
#endif
    outbuf = (float *)jack_port_get_buffer((*jack_p)->port, fragment_size);
    memset(outbuf, 0, fragment_size*sizeof(float));

    // Checking for convChannels with output connected to this jackaudio output
    for(vector<ConvChannel*>::iterator channel_p = (*jack_p)->channels.begin() ; channel_p != (*jack_p)->channels.end(); channel_p++) {
#ifdef RTDEBUG
      std::cout << "ioJack callback: passing output data from convolver: " << (*channel_p)->get_index() << std::endl;
#endif
      (*channel_p)->fillOutputBuffer((*jack_p)->port_name, outbuf);
    }
    // Check for nae output channels conected to this jackaudio output
    for(vector<nae_channel*>::iterator pn_ch = (*jack_p)->nae_channels.begin() ; pn_ch != (*jack_p)->nae_channels.end(); pn_ch++) {
#ifdef RTDEBUG
      std::cout << "ioJack callback: passing input data to nae: " << (*pn_ch)->n_nae->getName() << "/" << (*pn_ch)->n_side << std::endl;
#endif	
	(*pn_ch)->n_nae->fillOutputBuffer((*pn_ch)->n_side, outbuf);
    }
  }

  // Processing convChannels (convolver  process) output 
  for (vector<ConvChannel*>::iterator channel_p = conv_channels.begin() ; channel_p != conv_channels.end(); channel_p++) {
#ifdef RTDEBUG
    std::cout << "ioJack callback: processing output convolver: " << (*channel_p)->get_index() << std::endl;
#endif	
      (*channel_p)->processOutput(fragment_size);
    }

  // Running NAE processes (threads)
#ifdef RTDEBUG
  std::cout << "ioJack callback: NAE processing..." << std::endl;
#endif
  for (vector<NAE*>::iterator nae_p = nae_channels.begin() ; nae_p != nae_channels.end(); nae_p++) {
    (*nae_p)->signal();
    }

#ifdef RTDEBUG
  std::cout << "ioJack callback: finished" << std::endl;
#endif
  return(0);
}

bool ioJack::global_init(void)
{
  jack_status_t status;
  struct sched_param spar;
  jack_set_error_function(error_callback);
  if ((jackclient = jack_client_open(client_name, JackNoStartServer, &status)) == NULL) {
    throw std::runtime_error("JACK I/O: Could not become JACK client (status: 0x" + std::to_string(status) + "Error message(s):\n");
    if ((status & JackFailure) != 0) {
      throw std::runtime_error("Overall operation failed.\n");
      return false;
    }
    if ((status & JackInvalidOption) != 0) {
      throw std::runtime_error("  Likely bug in natambio: the operation contained an invalid or unsupported\noption.\n");
      return false;
    }
    if ((status & JackNameNotUnique) != 0) {
      throw std::runtime_error("  Client name \"" + std::string(client_name) + "\" not unique, try another name.\n");
      return false;
    }
    if ((status & JackServerFailed) != 0) {
      throw std::runtime_error("  Unable to connect to the JACK server. Perhaps it is not running? natambio\nrequires that a JACK server is started in advance.\n");
      return false;
    }
    if ((status & JackServerError) != 0) {
      throw std::runtime_error("  Communication error with the JACK server.\n");
      return false;
    }
    if ((status & JackNoSuchClient) != 0) {
      throw std::runtime_error("  Requested client does not exist.\n");
      return false;
    }
    if ((status & JackLoadFailure) != 0) {
      throw std::runtime_error("  Unable to load internal client.\n");
      return false;
    }
    if ((status & JackInitFailure) != 0) {
      throw std::runtime_error("  Unable initialize client.\n");
      return false;
    }
    if ((status & JackShmFailure) != 0) {
      throw std::runtime_error("  Unable to access shared memory.\n");
      return false;
    }
    if ((status & JackVersionError) != 0) {
      throw std::runtime_error("The version of the JACK server is not compatible with the JACK client\nlibrary used by natambio.\n");
      return false;
    }
    sleep(1);
    return false;
  }
  sample_rate = (int)jack_get_sample_rate(jackclient);
  fragment_size = jack_get_buffer_size(jackclient);
  if (fragment_size < 32) {
    throw std::runtime_error("Fragment size is too small\n");
    return false;
  }
  if (fragment_size > 4096) {
    throw std::runtime_error("Fragment size is too large\n");
    return false;
  }
  if (jack_is_realtime(jackclient) == 0)
    throw std::runtime_error("JACK I/O: Warning: JACK is not running with SCHED_FIFO or SCHED_RR (realtime).\n");

  priority = jack_client_real_time_priority(jackclient);
  pthread_getschedparam (jack_client_thread_id(jackclient), &policy, &spar);

  if(!quiet) {
    std::cout << "ioJack: Jackaudio sample rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "ioJack: Jackaudio partition size: " << fragment_size << " samples" << std::endl;
    if(policy == SCHED_RR)
      std::cout << "ioJack: Jackaudio SCHED_RR policy" << std::endl;
    else if(policy == SCHED_FIFO)
      std::cout << "ioJack: Jackaudio SCHED_FIFO policy" << std::endl;
    std::cout << "ioJack: Jackaudio RT priority: " << priority << std::endl;
  }
  policy = SCHED_RR;
  jack_set_xrun_callback(jackclient, ioJack::xrun_callback, this);
  jack_set_process_callback(jackclient, ioJack::jack_process_callback, this);
  jack_on_shutdown(jackclient, ioJack::jack_shutdown_callback, this);
  
  return true;
}

bool ioJack::addInputPort(string port_name)
{
  jack_port_t *new_jack_input;
  string connect_port_name;
  new_jack_input = jack_port_register(jackclient, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  if(new_jack_input == NULL) {
    throw std::runtime_error("JACK I/O: Cannot register port "+ port_name + " \n");
    return false;
  }
  jack_inputs.push_back(new jack_port);
  jack_inputs.back()->port = new_jack_input;
  jack_inputs.back()->port_name = port_name;
  
  if(!quiet) 
    std::cout << "ioJack: New jack input port added: " << client_name << ":" << port_name << std::endl;
  return true;
}

bool ioJack::addOutputPort(string port_name)
{
  jack_port_t *new_jack_output;
  string connect_port_name;
  new_jack_output = jack_port_register(jackclient, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  if(new_jack_output == NULL) {
    throw std::runtime_error("JACK I/O: Cannot register port "+ port_name + " \n");
    return false;
  }
  jack_outputs.push_back(new jack_port);
  jack_outputs.back()->port = new_jack_output;
  jack_outputs.back()->port_name = port_name;

  if(!quiet) 
    std::cout << "ioJack: New jack output port added: " << client_name << ":" << port_name << std::endl;
  return true;
}

void ioJack::ConnectInputPort(string port_name, string dest_name)
{
  string connect_port_name;
  int connected;
  if(dest_name.size() > 0) {
    connect_port_name = (string)client_name + ":" + port_name;
    connected = connect_port(dest_name, connect_port_name);
    if(connected == 0) {
      if(!quiet) 
	std::cout << "ioJack: Input port connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    } else if(connected == EEXIST)
      throw std::runtime_error("JACK I/O: connection already exists "+ connect_port_name + "<----->" + dest_name + " \n");
  }
}

void ioJack::ConnectOutputPort(string port_name, string dest_name)
{
  string connect_port_name;
  int connected;
  if(dest_name.size() > 0) {
    connect_port_name = (string)client_name + ":" + port_name;
    connected = connect_port(connect_port_name, dest_name);
    if(connected == 0) {
      if(!quiet)
	std::cout << "ioJack: Output port connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    } else if(connected == EEXIST)
      throw std::runtime_error("JACK I/O: connection already exists "+ connect_port_name + "<----->" + dest_name + " \n");
  }
}

void ioJack::addConvChannel(ConvChannel* conv_channel)
{
  if(!quiet)
    std::cout << "ioJack: Convolver channel added: " << conv_channel->get_index() << std::endl;
  conv_channels.push_back(conv_channel);
}

void ioJack::addNaeChannel(NAE* n_nae)
{
  if(!quiet)
    std::cout << "ioJack: NAE input added: " << n_nae->getName() << std::endl;
  nae_channels.push_back(n_nae);
}

bool ioJack::connectInputConvPort(string port_name, ConvChannel* channel)
{
  bool found = false;
  for (std::vector<struct jack_port*>::iterator it = jack_inputs.begin() ; it != jack_inputs.end(); ++it) 
    if((*it)->port_name == port_name) {
      if(!quiet)
	std::cout << "ioJack: Connected jackaudio input port "<< port_name << " to convolver port index " << channel->get_index() << std::endl;
      (*it)->channels.push_back(channel);
      found = true;
    }
  if(!found)
    throw std::runtime_error("JACK I/O: Input port name \""+ port_name + "\" not found. Convolver input cannot be connected to \n");
  return found;
}

bool ioJack::connectOutputConvPort(string port_name, ConvChannel* channel)
{
  bool found = false;
  for (std::vector<struct jack_port*>::iterator it = jack_outputs.begin() ; it != jack_outputs.end(); ++it) 
    if((*it)->port_name == port_name) {
      if(!quiet)
	std::cout << "ioJack: Connected jackaudio output port "<< port_name << " to convolver port index " << channel->get_index() << std::endl;
      (*it)->channels.push_back(channel);
      found = true;
    }
  if(!found)
    throw std::runtime_error("JACK I/O: Output port name \"" + port_name + "\" not found. Convolver output cannot be connected to \n");
  return found;
}

bool ioJack::connectInputNaePort(enum side n_side, string port_name, NAE* n_nae)
{
  bool found = false;
  for (std::vector<struct jack_port*>::iterator it = jack_inputs.begin() ; it != jack_inputs.end(); ++it) 
    if((*it)->port_name == port_name) {
      if(!quiet)
	std::cout << "ioJack: Connected jackaudio input port "<< port_name << " to NAE channel " << n_nae->getChannelIn(n_side) << std::endl;
      struct nae_channel *n_nae_ch =  new struct nae_channel;
      n_nae_ch->n_side = n_side;
      n_nae_ch->n_nae = n_nae;
      (*it)->nae_channels.push_back(n_nae_ch);
      found = true;
    }
  if(!found)
    throw std::runtime_error("JACK I/O: Input port name \""+ port_name + "\" not found. NAE input cannot be connected to \n");
  return found;
}

bool ioJack::connectOutputNaePort(enum side n_side, string port_name, NAE* n_nae)
{
  bool found = false;
  for (std::vector<struct jack_port*>::iterator it = jack_outputs.begin() ; it != jack_outputs.end(); ++it) 
    if((*it)->port_name == port_name) {
      if(!quiet)
	std::cout << "ioJack: Connected jackaudio output port "<< port_name << " to NAE channel " << n_nae->getChannelOut(n_side) << std::endl;
      struct nae_channel *n_nae_ch =  new struct nae_channel;
      n_nae_ch->n_side = n_side;
      n_nae_ch->n_nae = n_nae;
      (*it)->nae_channels.push_back(n_nae_ch);
      found = true;
    }
  if(!found)
    throw std::runtime_error("JACK I/O: Output port name \""+ port_name + "\" not found. NAE output cannot be connected to \n");
  return found;
}
 
int ioJack::synch_start(void)
{
  int n;

  if (has_started) {
    return 0;
  }
  if (jackclient == NULL) {
    throw std::runtime_error("JACK I/O: client is NULL\n");
    return -1;
  }
  has_started = true;
  n = jack_activate(jackclient);
  if (n != 0) {
    throw std::runtime_error("JACK I/O: Could not activate local JACK client.\n");
    has_started = false;
    return -1;
  }
  if(!quiet) 
    std::cout << "ioJack: Jackaudio client "<< client_name << " has started" << std::endl;
  return 0;
}

bool ioJack::connect_port(string port_name, string dest_name) 
{
  if (jack_connect(jackclient, port_name.c_str(), dest_name.c_str()) != 0) {
    string msg = "JACK I/O: Could not connect local port " + port_name + " to " + dest_name + "\n";
    throw std::runtime_error(msg.c_str());
    return false;
  } else {
    return true;
  }
}

bool ioJack::disconnect_port(string port_name, string dest_name) 
{
  if (jack_disconnect(jackclient, port_name.c_str(), dest_name.c_str()) != 0) {
    string msg = "JACK I/O: Could not disconnect local port " + port_name + " to " + dest_name + "\n";
    throw std::runtime_error(msg.c_str());
    return false;
  } else {
    return true;
  }
}

const char ** ioJack::get_jack_port_connections(string port_name)
{
  jack_port_t *port;

  port = jack_port_by_name(jackclient, port_name.c_str());
  if (port == NULL) 
    {
      string msg = "JACK I/O: Can't find port %s\n" + port_name +"\n";
      throw std::runtime_error(msg.c_str());
      return NULL;
    }
  return jack_port_get_all_connections(jackclient, port);
}

const char **ioJack::get_jack_ports(void)
{
  return jack_get_ports(jackclient, NULL, NULL, 0);
}

const char **ioJack::get_jack_input_physical_ports(void)
{
  return jack_get_ports(jackclient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsPhysical);
}

const char **ioJack::get_jack_input_ports(void)
{
  return jack_get_ports(jackclient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
}

const char **ioJack::get_jack_output_physical_ports(void)
{
  return jack_get_ports(jackclient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsPhysical);
}

const char **ioJack::get_jack_output_ports(void)
{
  return jack_get_ports(jackclient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
}

void ioJack::synch_stop(void)
{
  has_started = false;
  jack_deactivate(jackclient);
  jack_client_close(jackclient);
}    
