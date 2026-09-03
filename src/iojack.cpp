/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
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
  /* Start muted and aim at unity: the outputs fade in over the first periods
     after activation instead of jumping to whatever level the input is already
     at. global_init() replaces ramp_inc once the sample rate is known. */
  ramp_gain = 0.0f;
  ramp_target = 1.0f;
  ramp_inc = 1.0f / 1536.0f;
  mute_target = 0;
  if (client_name == NULL) 
    client_name = strdup(DEFAULT_CLIENTNAME);
  if(!quiet)
    std::cout << "iojack: new Jackaudio client: " << client_name << std::endl;
}

ioJack::~ioJack(void)
{
  /* Stop the client BEFORE freeing anything the RT callback uses. Everything
     below -- the jack_port structs, the nae_channel structs, the NAE objects
     with the output buffers fillOutputBuffer() reads from -- is live for the
     callback until jack_deactivate() returns, and deactivate is what
     guarantees it has finished and will not run again. Freeing first left the
     callback reading freed memory for as long as the teardown took (the NAE
     destructors join their worker threads), and it writes whatever it reads
     straight to the outputs: a burst of garbage at the speakers on every
     stop. Not academic -- it is the audible pulse on systemctl restart. */
  synch_stop();
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

/* Called by libjack from its own C frames (a failed jack_connect() reports here
   first), so this must not throw: unwinding through C code is undefined, and it
   would abort the process before the caller could act on the return value it is
   about to get. Report and let the caller decide. */
void ioJack::error_callback(const char* msg)
{
  std::cerr << "ioJack: JACK reported an error: " << msg << std::endl;
}

int ioJack::na_process_callback(jack_nframes_t n_frames)
{
  float *inpbuf, *outbuf;

  /* The period starts being counted here and stops at the return: everything
     the callback does, which is the figure the JACK period has to hold. */
  cycle_time.begin();

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

    /* Apply the port's <gain>, slewed across the period towards its target so
       that a change arriving from the remote manager fades rather than steps.
       JACK's input buffer is shared with every other client reading the same
       source port, so it is never scaled in place: the scaled samples go to
       the port's own buffer and everything downstream reads that instead. At
       unity, with nothing to slew towards, there is no copy at all. */
    float iga = (*jack_p)->gain;
    float igb = slewGain(iga, (*jack_p)->gain_target);
    (*jack_p)->gain = igb;
    if(iga != 1.0f || igb != 1.0f) {
      float *scaled = (*jack_p)->gain_buf.data();
      float g = iga;
      float gstep = (igb - iga) / (float)fragment_size;
      for(int i = 0; i < fragment_size; i++, g += gstep)
        scaled[i] = inpbuf[i] * g;
      inpbuf = scaled;
    }

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
  
  /* The convolution stage, first piece: the input sums and the convolution
     itself, which run back to back. It is closed before the output ports are
     filled and opened again for processOutput() below, so that the port
     plumbing in between is not counted as convolution. */
  conv_time.begin();

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

  conv_time.accumulate();

#ifdef RTDEBUG
  std::cout << "ioJack callback: convproc processing finished..." << std::endl;
#endif

  /* Ramp for this period, computed once so the whole output bus fades
     together: g0 at the first sample, g1 at the last, interpolated in between.
     While ramp_gain sits at ramp_target -- the running case -- both ends are
     1.0 and the scaling loop below is skipped altogether. */
  float g0 = ramp_gain;
  float g1 = g0;
  float span = ramp_inc * (float)fragment_size;
  if(ramp_target > g0) {
    g1 = g0 + span;
    if(g1 > ramp_target)
      g1 = ramp_target;
  } else if(ramp_target < g0) {
    g1 = g0 - span;
    if(g1 < ramp_target)
      g1 = ramp_target;
  }

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

  /* Second piece of the convolution stage: the delay, the scale and the mix
     into the output ports. commit() below adds it to the first and writes the
     cycle's one sample. */
  conv_time.begin();

  // Processing convChannels (convolver  process) output 
  for (vector<ConvChannel*>::iterator channel_p = conv_channels.begin() ; channel_p != conv_channels.end(); channel_p++) {
#ifdef RTDEBUG
    std::cout << "ioJack callback: processing output convolver: " << (*channel_p)->get_index() << std::endl;
#endif	
      (*channel_p)->processOutput(fragment_size);
    }
  conv_time.end();

  /* Scale the outputs only here, with everything that feeds them already summed
     in. The loop above cannot do it: ConvChannel::fillOutputBuffer() does not
     write anything, it just hands the channel the port buffer, and the samples
     land in processOutput(), which has only now run. Scaling any earlier leaves
     every convolution output unscaled -- neither faded nor trimmed -- while the
     NAE outputs, which fillOutputBuffer() does sum in place, get scaled.
     Each port's <gain> is folded into the period's ramp so the buffer is walked
     once. The ramp itself is left alone: ramp_gain must keep tracking the fade,
     not the fade times a port gain. */
  /* Mute, read once for the whole bus so that every output leaves and rejoins
     it together. It only changes where each port is heading: the slew below is
     the same one a gain change uses, so muting fades out and unmuting fades
     back in over the same few tens of milliseconds. */
  int muted = mute_target;
  float mute_gain = (float)FROM_DB(NA_MUTE_DB);

  for (vector<jack_port*>::iterator jack_p = jack_outputs.begin() ; jack_p != jack_outputs.end(); jack_p++) {
    float otgt = muted ? mute_gain : (*jack_p)->gain_target;
    float oga = (*jack_p)->gain;
    float ogb = slewGain(oga, otgt);
    (*jack_p)->gain = ogb;
    float pg0 = g0 * oga;
    float pg1 = g1 * ogb;
    if(pg0 == 1.0f && pg1 == 1.0f)
      continue;
    outbuf = (float *)jack_port_get_buffer((*jack_p)->port, fragment_size);
    float pgstep = (pg1 - pg0) / (float)fragment_size;
    float g = pg0;
    for(int i = 0; i < fragment_size; i++, g += pgstep)
      outbuf[i] *= g;
  }
  ramp_gain = g1;

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

  cycle_time.end();
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
  // jack_client_open() is called without JackUseExactName, so JACK does not fail
  // on a duplicate name: it grants a unique variant ("natambio-01") instead. Adopt
  // the granted name here -- ConnectInputPort()/ConnectOutputPort() build their
  // "<client>:<port>" strings from client_name, and using the requested name would
  // make every connection target a client that does not exist. Keeping this in
  // sync is what allows several natambio instances to run in parallel from the
  // same XML (one pipe per instance, switched at the JACK graph).
  const char *granted_name = jack_get_client_name(jackclient);
  if (granted_name != NULL && strcmp(granted_name, client_name) != 0) {
    std::cerr << "ioJack: JACK client name \"" << client_name << "\" was already in use; "
              << "running as \"" << granted_name << "\"" << std::endl;
    char *adopted = strdup(granted_name);
    if (adopted != NULL) {
      free(client_name);
      client_name = adopted;
    }
  }

  sample_rate = (int)jack_get_sample_rate(jackclient);
  fragment_size = jack_get_buffer_size(jackclient);
  /* 32 ms of ramp: long enough that the fade carries no audible step of its
     own, short enough to fit well inside the service's TimeoutStopSec. */
  ramp_inc = 1.0f / (0.032f * (float)sample_rate);
  if (fragment_size < 32) {
    throw std::runtime_error("Fragment size is too small\n");
    return false;
  }
  if (fragment_size > 4096) {
    throw std::runtime_error("Fragment size is too large\n");
    return false;
  }
  // Realtime JACK is a hard requirement, not a warning: the convolver partitions
  // and the NAE threads run at the priority JACK hands out, and without SCHED_FIFO
  // or SCHED_RR they get no scheduling guarantee at all. Report and give up here
  // rather than throwing -- global_init()'s bool return is handled all the way up
  // to main(), so the user sees this message instead of an uncaught-exception abort.
  if (jack_is_realtime(jackclient) == 0) {
    std::cerr << "ioJack: JACK is running without realtime scheduling (neither SCHED_FIFO\n"
              << "        nor SCHED_RR). natambio requires a realtime JACK server and will\n"
              << "        not start without one.\n"
              << "        Start the server with realtime scheduling enabled, for example:\n"
              << "            jackd -R -P 70 -d alsa -d hw:0 -r 48000 -p 256 -n 3\n"
              << "        If jackd refuses to go realtime, check that the user may take\n"
              << "        realtime priorities (membership of the audio group and the\n"
              << "        rtprio/memlock limits in /etc/security/limits.d)." << std::endl;
    return false;
  }

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

bool ioJack::addInputPort(string port_name, double gain_db)
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
  jack_inputs.back()->gain_db = gain_db;
  jack_inputs.back()->gain_target = (float)FROM_DB(gain_db);
  jack_inputs.back()->gain = jack_inputs.back()->gain_target;
  /* Every input port gets its private copy, allocated here, out of the RT
     thread, whether or not this gain needs it: the remote manager can take any
     port off unity later and the callback cannot allocate. fragment_size is
     already known -- global_init() runs before any port is added. */
  jack_inputs.back()->gain_buf.resize(fragment_size);
  
  if(!quiet) 
    std::cout << "ioJack: New jack input port added: " << client_name << ":" << port_name
              << " (gain " << gain_db << " dB)" << std::endl;
  return true;
}

bool ioJack::addOutputPort(string port_name, double gain_db)
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
  /* Outputs own their buffer, so the gain is folded into the fade ramp the
     callback already applies in place: no copy needed. */
  jack_outputs.back()->gain_db = gain_db;
  jack_outputs.back()->gain_target = (float)FROM_DB(gain_db);
  jack_outputs.back()->gain = jack_outputs.back()->gain_target;

  if(!quiet) 
    std::cout << "ioJack: New jack output port added: " << client_name << ":" << port_name
              << " (gain " << gain_db << " dB)" << std::endl;
  return true;
}

/* Find a registered port by name, inputs and outputs alike: JACK names are
   unique within a client, so one namespace is enough and the caller does not
   have to know which direction a name belongs to. NULL if there is none. */
struct jack_port *ioJack::findPort(string port_name)
{
  for (vector<jack_port*>::iterator jack_p = jack_inputs.begin() ; jack_p != jack_inputs.end(); jack_p++)
    if((*jack_p)->port_name == port_name)
      return *jack_p;
  for (vector<jack_port*>::iterator jack_p = jack_outputs.begin() ; jack_p != jack_outputs.end(); jack_p++)
    if((*jack_p)->port_name == port_name)
      return *jack_p;
  return NULL;
}

/* Called from the manager thread. One store into the word the callback reads;
   the ports' own gains are not touched, so up/down and get go on meaning what
   they meant and the ports come back to exactly where they were. */
void ioJack::setMute(bool on)
{
  mute_target = on ? 1 : 0;
  if(!quiet)
    std::cout << "ioJack: outputs " << (on ? "muted" : "unmuted") << std::endl;
}

/* By <name>, the only handle the configuration gives an engine. An engine
   configured without one cannot be addressed and is skipped rather than
   matched by an empty string, which would make every nameless engine the
   answer to the same command. */
NAE *ioJack::findNae(string nae_name)
{
  if(nae_name.empty())
    return NULL;
  for (vector<NAE*>::iterator nae_p = nae_channels.begin() ; nae_p != nae_channels.end(); nae_p++)
    if((*nae_p)->getName() == nae_name)
      return *nae_p;
  return NULL;
}

vector<string> ioJack::naeNames(void)
{
  vector<string> names;
  for (vector<NAE*>::iterator nae_p = nae_channels.begin() ; nae_p != nae_channels.end(); nae_p++)
    if(!(*nae_p)->getName().empty())
      names.push_back((*nae_p)->getName());
  return names;
}

/* Everything the engine was configured with, gains included, in one call: a
   report of it is a snapshot and would be a misleading one if the gains could
   move between the reads that made it. */
static void nae_config_fill(NAE *nae, struct nae_config *cfg)
{
  cfg->name = nae->getName();
  cfg->mode = nae->getMode();
  cfg->steps_length = nae->getCovStepsLength();
  cfg->pan_scale = nae->getPanScale();
  cfg->front_gain_db = nae->gainDb(NAE_GAIN_FRONT);
  cfg->ambience_gain_db = nae->gainDb(NAE_GAIN_AMB);
  cfg->rear_gain_db = nae->gainDb(NAE_GAIN_REAR);
  cfg->input_left = nae->getChannelIn(LEFT);
  cfg->input_right = nae->getChannelIn(RIGHT);
  cfg->output_left = nae->getChannelOut(LEFT);
  cfg->output_right = nae->getChannelOut(RIGHT);
  cfg->front_output_left = nae->getChannelOut(C1_LEFT);
  cfg->front_output_right = nae->getChannelOut(C1_RIGHT);
  cfg->amb_output_left = nae->getChannelOut(C2_LEFT);
  cfg->amb_output_right = nae->getChannelOut(C2_RIGHT);
}

bool ioJack::naeConfig(string nae_name, struct nae_config *cfg)
{
  NAE *nae = findNae(nae_name);
  if(nae == NULL)
    return false;
  if(cfg != NULL)
    nae_config_fill(nae, cfg);
  return true;
}

bool ioJack::naeConfigAt(size_t index, struct nae_config *cfg)
{
  if(index >= nae_channels.size())
    return false;
  if(cfg != NULL)
    nae_config_fill(nae_channels[index], cfg);
  return true;
}

bool ioJack::naePanScale(string nae_name, double *scale)
{
  NAE *nae = findNae(nae_name);
  if(nae == NULL)
    return false;
  if(scale != NULL)
    *scale = nae->getPanScale();
  return true;
}

bool ioJack::setNaePanScale(string nae_name, double scale, double *new_scale)
{
  NAE *nae = findNae(nae_name);
  if(nae == NULL)
    return false;
  if(!nae->setLivePanScale(scale))
    return false;
  if(new_scale != NULL)
    *new_scale = nae->getPanScale();
  if(!quiet)
    std::cout << "ioJack: NAE " << nae_name << " pan scale now " << std::fixed
              << std::setprecision(3) << scale << std::endl;
  return true;
}

bool ioJack::naeGain(string nae_name, enum nae_gain which, double *gain_db, bool *active)
{
  NAE *nae = findNae(nae_name);
  if(nae == NULL)
    return false;
  if(gain_db != NULL)
    *gain_db = nae->gainDb(which);
  if(active != NULL)
    *active = nae->gainActive(which);
  return true;
}

/* Called from the remote manager's thread, like adjustPortGain(): NAE::setGainDb()
   writes the dB value it owns and then, in one store, the single word the
   engine's worker thread reads. The engine slews to it across the next block,
   so what comes out is a fade of a few tens of milliseconds and not a step. */
bool ioJack::setNaeGain(string nae_name, enum nae_gain which, double db,
                        double *new_gain_db, bool *active)
{
  NAE *nae = findNae(nae_name);
  if(nae == NULL)
    return false;
  double g = nae->setGainDb(which, db);
  bool on = nae->gainActive(which);
  if(new_gain_db != NULL)
    *new_gain_db = g;
  if(active != NULL)
    *active = on;
  if(!quiet) {
    const char *label = (which == NAE_GAIN_FRONT) ? "front" :
                        (which == NAE_GAIN_AMB)   ? "amb" : "rear";
    std::cout << "ioJack: NAE " << nae_name << " " << label << " gain now "
              << std::fixed << std::setprecision(3) << g << " dB"
              << (on ? "" : " (not used in this mode)") << std::endl;
  }
  return true;
}

/* One engine's per-block times, by position. Answered from the manager's
   thread, and the engine goes on decomposing while it is read -- see
   cycletime.hpp for what that costs and why it costs nothing worth locking
   against. */
bool ioJack::naeTimeStatsAt(size_t index, struct na_time_stats *st, string *name)
{
  if(index >= nae_channels.size())
    return false;
  if(st != NULL)
    nae_channels[index]->timeStats(st);
  if(name != NULL)
    *name = nae_channels[index]->getName();
  return true;
}

/* Every timer at once, the callback's two and the engines'. They are cleared
   one after another rather than together -- there is no moment at which all of
   them stop -- so the first cycles after this may be counted by one timer and
   not by the next. Over a window of a thousand it is the difference between a
   measurement starting on this period or the following one. */
void ioJack::resetTimeStats(void)
{
  cycle_time.reset();
  conv_time.reset();
  for (vector<NAE*>::iterator nae_p = nae_channels.begin() ; nae_p != nae_channels.end(); nae_p++)
    (*nae_p)->resetTimeStats();
}

vector<string> ioJack::inputPortNames(void)
{
  vector<string> names;
  for (vector<jack_port*>::iterator jack_p = jack_inputs.begin() ; jack_p != jack_inputs.end(); jack_p++)
    names.push_back((*jack_p)->port_name);
  return names;
}

vector<string> ioJack::outputPortNames(void)
{
  vector<string> names;
  for (vector<jack_port*>::iterator jack_p = jack_outputs.begin() ; jack_p != jack_outputs.end(); jack_p++)
    names.push_back((*jack_p)->port_name);
  return names;
}

vector<string> ioJack::portNames(void)
{
  vector<string> names = inputPortNames();
  vector<string> outs = outputPortNames();
  names.insert(names.end(), outs.begin(), outs.end());
  return names;
}

bool ioJack::portGain(string port_name, double *gain_db)
{
  struct jack_port *p = findPort(port_name);
  if(p == NULL)
    return false;
  if(gain_db != NULL)
    *gain_db = p->gain_db;
  return true;
}

/* Add delta_db to a port's gain. Called from the remote manager's thread: it
   owns gain_db, and the single word the RT callback reads from it is
   gain_target, written last and in one store. The callback slews to it, so
   what comes out is a fade of a few tens of milliseconds rather than a step.
   The clamp is not a matter of taste: these commands are relative, so nothing
   else stops a repeated "up" from arriving at a gain that damages a driver. */
bool ioJack::adjustPortGain(string port_name, double delta_db, double *new_gain_db)
{
  struct jack_port *p = findPort(port_name);
  if(p == NULL)
    return false;
  double g = p->gain_db + delta_db;
  if(g > NA_PORT_GAIN_MAX_DB) g = NA_PORT_GAIN_MAX_DB;
  if(g < NA_PORT_GAIN_MIN_DB) g = NA_PORT_GAIN_MIN_DB;
  p->gain_db = g;
  p->gain_target = (float)FROM_DB(g);
  if(new_gain_db != NULL)
    *new_gain_db = g;
  if(!quiet)
    std::cout << "ioJack: port " << port_name << " gain now " << std::fixed
              << std::setprecision(3) << g << " dB" << std::endl;
  return true;
}

/* An empty <destname> means "register the port but leave it unconnected", which
   is how a pipe is parked for later patching, so it is not a failure. A
   connection that already exists is not one either. Anything else is: the
   configured signal path is not what the XML asked for, and going on would run
   natambio with a silently broken chain. */
bool ioJack::ConnectInputPort(string port_name, string dest_name)
{
  string connect_port_name;
  int connected;
  if(dest_name.size() == 0)
    return true;
  connect_port_name = (string)client_name + ":" + port_name;
  connected = connect_port(dest_name, connect_port_name);
  if(connected == 0) {
    if(!quiet)
      std::cout << "ioJack: Input port connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    return true;
  }
  if(connected == EEXIST) {
    if(!quiet)
      std::cout << "ioJack: Input port already connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    return true;
  }
  std::cerr << "ioJack: could not connect input port " << dest_name << " -----> " << connect_port_name
            << " (JACK returned " << connected << "). Check that " << dest_name
            << " exists and is a source port (jack_lsp)." << std::endl;
  return false;
}

bool ioJack::ConnectOutputPort(string port_name, string dest_name)
{
  string connect_port_name;
  int connected;
  if(dest_name.size() == 0)
    return true;
  connect_port_name = (string)client_name + ":" + port_name;
  connected = connect_port(connect_port_name, dest_name);
  if(connected == 0) {
    if(!quiet)
      std::cout << "ioJack: Output port connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    return true;
  }
  if(connected == EEXIST) {
    if(!quiet)
      std::cout << "ioJack: Output port already connected: " << connect_port_name << "<----->" << dest_name << std::endl;
    return true;
  }
  std::cerr << "ioJack: could not connect output port " << connect_port_name << " -----> " << dest_name
            << " (JACK returned " << connected << "). Check that " << dest_name
            << " exists and is a sink port (jack_lsp)." << std::endl;
  return false;
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

/* Pass JACK's own status through to the caller: 0 on success, EEXIST when the
   connection is already made, any other non-zero on failure. The callers need to
   tell those three apart -- an existing connection is not a problem, a refused
   one is -- and they could not when this returned a bool and threw on failure. */
int ioJack::connect_port(string port_name, string dest_name)
{
  return jack_connect(jackclient, port_name.c_str(), dest_name.c_str());
}

int ioJack::disconnect_port(string port_name, string dest_name)
{
  return jack_disconnect(jackclient, port_name.c_str(), dest_name.c_str());
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

/* The wait is bounded because the callback is not guaranteed to run at all: if
   the server died or never activated us, ramp_gain never moves and this would
   hang the shutdown. 200 ms is six times the ramp itself and still well inside
   the service's TimeoutStopSec. */
void ioJack::fadeOut(int timeout_ms)
{
  if(!has_started || jackclient == NULL)
    return;
  ramp_target = 0.0f;
  for(int waited = 0; waited < timeout_ms && ramp_gain > 0.0f; waited++)
    usleep(1000);
}

void ioJack::synch_stop(void)
{
  if(jackclient == NULL)
    return;
  /* Fade before deactivating. Once the client is gone JACK feeds the hardware
     ports zeros, so leaving on a non-zero sample is a step straight to the
     converters -- the tail of the music cut mid-waveform. */
  fadeOut();
  has_started = false;
  jack_deactivate(jackclient);
  jack_client_close(jackclient);
  jackclient = NULL;
}    
