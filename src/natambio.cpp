/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#include "natambio.hpp"

NatAmbio::NatAmbio(void)
{
  quiet = false;
  naJack = NULL;
  naConf = new NaConf();
  convproc = new Convproc();
  options = 0;
  convproc->set_options(options);
  sampleRate = 0;
}

void NatAmbio::setQuiet(void)
{
  quiet = true;
  naConf->setQuiet();
}

/* Open a minimal, short-lived JACK client just to read the running server's
 * sample rate, then close it. Done before parsing so NaConf can generate the
 * xtc / low_and_high_filter / loudness coeffs at the JACK rate (no
 * <sample_rate> tag needed) and validate every WAV against it. Returns the
 * sample rate in Hz, or 0 if JACK is unreachable. */
int NatAmbio::queryJackSampleRate(void)
{
  jack_status_t status;
  jack_client_t *probe = jack_client_open("natambio_sr_probe", JackNoStartServer, &status);
  if(probe == NULL) {
    if(!quiet)
      cerr << "NatAmbio: cannot reach JACK to query sample rate "
	   << "(is jackd running?). status=0x" << std::hex << status << std::dec << endl;
    return 0;
  }
  int sr = (int) jack_get_sample_rate(probe);
  jack_client_close(probe);
  if(!quiet)
    cout << "NatAmbio: JACK sample rate: " << sr << " Hz" << endl;
  return sr;
}

bool NatAmbio::configXML(string fileName)
{
  bool result;
  // Probe the JACK sample rate first; coeff generation and WAV validation in
  // NaConf depend on it, so abort here if JACK is not available.
  sampleRate = queryJackSampleRate();
  if(sampleRate <= 0) {
    if(!quiet)
      cout << "NatAmbio: could not determine JACK sample rate; aborting." << endl;
    delete naConf;
    naConf = NULL;
    delete convproc;
    convproc = NULL;
    return false;
  }
  if(!(result = naConf->conf_init(fileName, sampleRate))) {
    if(!quiet)
      cout << "NatAmbio: Error in configXML " << endl;
    delete naConf;
    naConf = NULL;
    delete convproc;
    convproc = NULL;
  }
  return result;
}

bool NatAmbio::jackStart(void)
{
  naJack = new ioJack(naConf->jackclient->name, quiet);
  if(!(naJack->global_init())) {
    delete naJack;
    naJack = NULL;
    return false;
  }
  sampleRate = naJack->getSampleRate();
  naJack->setconvproc(convproc);
  for(std::vector<struct jackport*>::iterator it = naConf->jackclient->inports.begin() ; it != naConf->jackclient->inports.end(); ++it) {
    if(!quiet) 
      cout << "NatAmbio: Creating input port " << (*it)->name << endl;
    if(!(naJack->addInputPort((*it)->name))) {
      delete naJack;
      naJack = NULL;
      return false;
    }
  }
  for(std::vector<struct jackport*>::iterator it = naConf->jackclient->outports.begin() ; it != naConf->jackclient->outports.end(); ++it) {
    if(!quiet) 
      cout << "NatAmbio: Creating output port " << (*it)->name << endl;
    if(!(naJack->addOutputPort((*it)->name))) {
      delete naJack;
      naJack = NULL;
      return false;
    }
  }
  return true;
}

bool NatAmbio::startConvProc(void)
{
  int dens = 1;
  int r_impdata, r_config, r_start;
  int part = naJack->getPartSize();
  int convidx = 0;
  bool found;
  bool found2;
  float *n_coeff = NULL;
  int n_length = 0;
  
  if(!quiet)
    std::cout << "NatAmbio: initial convproc configuration" << std::endl;
  if(!naConf->convollist.empty()) {
    r_config =convproc->configure(naConf->convollist.size(), naConf->convollist.size(), naConf->getMaxCoeffsSize(), part, part, Convproc::MAXPART, dens);
    switch(r_config) {
    case Converror::BAD_STATE: 
      throw std::runtime_error("NatAmbio: Can't initialise convproc convolver. Converror::BAD_STATE");
      return false;
      break;
    case Converror::BAD_PARAM: 
      throw std::runtime_error("NatAmbio: Can't initialise convproc convolver. Converror::BAD_PARAM");
      return false;
      break;
    case Converror::MEM_ALLOC: 
      throw std::runtime_error("NatAmbio: Can't initialise convproc convolver. Converror::MEM_ALLOC");
      return false;
      break;
    }
    for(std::vector<struct convol*>::iterator cv = naConf->convollist.begin(); cv != naConf->convollist.end(); ++cv) {
      found = false;
      for(std::vector<struct coeff*>::iterator cf = naConf->coefslist.begin(); cf != naConf->coefslist.end(); ++cf) {
	if((*cf)->name == (*cv)->coeff_name) {
	  found = true;
	  n_coeff = (*cf)->coeffs;
	  n_length = (*cf)->length;
	  // Sample-rate coherence is already enforced in NaConf (WAVs checked on
	  // load, generated coeffs created at the JACK rate), so no check here.
	  if(!quiet)
	    std::cout << "NatAmbio: creating impdata for convproc " << (*cv)->name << " with coeffs " << (*cf)->name << std::endl;
	  break;
	}
      }
      if((*cv)->coeff_name != "delta" && !found) {
	throw std::runtime_error("ConvProc: Coeffs name \""+ (*cv)->coeff_name + "\" not found. Convolver cannot be createad\n");
	return false;
      } else {
	r_impdata = convproc->impdata_create(convidx, convidx, 1, n_coeff, 0, n_length);
      }
      switch(r_impdata) {
      case Converror::BAD_STATE: 
	throw std::runtime_error("NatAmbio: error in convproc impdata_create. Converror:BAD_STATE");
	return false;
	break;
      case Converror::BAD_PARAM: 
	throw std::runtime_error("NatAmbio: error in convproc impdata_create. Converror::BAD_PARAM");
	return false;
	break;
      case Converror::MEM_ALLOC: 
	throw std::runtime_error("NatAmbio: error in convproc impdata_create. Converror::MEM_ALLOC");
	return false;
	break;
      }
      ConvChannel *newchannel = new ConvChannel(convproc, part, (*cv)->name, (*cv)->index, quiet);
      newchannel->set_name((*cv)->name);
      newchannel->set_delay((*cv)->delay);
      newchannel->set_scale((*cv)->scale);
      if((*cv)->coeff_name == "delta") {
	newchannel->set_bypass(true);
	newchannel->addBypassBuffer(part);
      }
      else {
	newchannel->set_bypass(false);
      }
      convChannels.push_back(newchannel);
      convidx++;
      naJack->addConvChannel(newchannel);
    }
    for(std::vector<struct convol*>::iterator cv = naConf->convollist.begin(); cv != naConf->convollist.end(); ++cv) {
      for(std::vector<ConvChannel*>::iterator cvchannel = convChannels.begin(); cvchannel != convChannels.end(); ++cvchannel) {
	if((*cv)->name == (*cvchannel)->get_name()) {
	  for(std::vector<string>::iterator inp = (*cv)->from_inputs.begin(); inp != (*cv)->from_inputs.end(); ++inp) {
	    for(std::vector<struct jackport*>::iterator it = naConf->jackclient->inports.begin() ; it != naConf->jackclient->inports.end(); ++it) {
	      found2 = false;
	      if((*inp) == (*it)->name) {
		found2 = true;
		(*cvchannel)->addInputBuffer(*inp);
		naJack->connectInputConvPort((*inp), *cvchannel);
		break;
	      }
	    }
	    if(!found2) {
	      throw std::runtime_error("NatAmbio: convolver " + (*cv)->name + " jack input port not found:"+ (*inp) + "\n");
	      return false;
	    }
	  }
	  for(std::vector<string>::iterator convinp = (*cv)->from_convols.begin(); convinp != (*cv)->from_convols.end(); ++convinp) {
	    if((*convinp) == (*cv)->name) {
	      throw std::runtime_error("NatAmbio: convolver input/output loop error "+ (*cv)->name + "\n");
	      return false;
	    }
	    found2 = false;
	    for(std::vector<ConvChannel*>::iterator cv2 = convChannels.begin(); cv2 != convChannels.end(); ++cv2) {
	      if((*convinp) == (*cv2)->get_name()) {
		if(!quiet) 
		  std::cout << "NatAmbio: creating convolver input to convproc " << (*cv)->name << " from convproc " << (*cv2)->get_index() << std::endl;
		found2 = true;
		(*cvchannel)->addConvProcBuffer(*cv2);
		break;
	      }
	    }
	    if(!found2) {
	      throw std::runtime_error("NatAmbio: convolver " + (*cv)->name + " input convproc not found:"+ (*convinp) + "\n");
	      return false;
	    }
	  }
	  for(std::vector<string>::iterator out = (*cv)->to_outputs.begin(); out != (*cv)->to_outputs.end(); ++out) {
	    found2 = false;
	    for(std::vector<struct jackport*>::iterator it = naConf->jackclient->outports.begin() ; it != naConf->jackclient->outports.end(); ++it) {
	      if((*out) == (*it)->name) {
		found2 = true;
		(*cvchannel)->addOutputBuffer(*out);
		naJack->connectOutputConvPort((*out), *cvchannel);
		break;
	      }
	    }
	    if(!found2) {
	      throw std::runtime_error("convProc: convolver " + (*cv)->name + " jack output port not found:"+ (*out) + "\n");
	      return false;
	    }
	  } 
	}
      }
    }
    if(!quiet) 
      std::cout << "NatAmbio convproc process start..." << std::endl;
    r_start = convproc->start_process(naJack->getPriority(), naJack->getPolicy());
    if (r_start == Converror::BAD_STATE) {
      throw std::runtime_error("NatAmbio: error in convproc impdata_create. Converror::BAD_STATE");
      return false;
    } else if(convproc->state() == Convproc::ST_PROC) {
      if(!quiet) 
	std::cout << "NatAmbio: convproc started in state Convproc::ST_PROC" << std::endl;
    }
  }
  else {
    if(!quiet) 
      std::cout << "NatAmbio: empty convproc process, not started ..." << std::endl;
  }
  startNAE();
  naJack->synch_start();
  return true;
}

bool NatAmbio::startNAE(void)
{
  bool found_left;
  bool found_right;
  bool found_nae;
  
  if(!quiet) {
    std::cout << "NatAmbio: number of NAE configurations " << naConf->naelist.size() << std::endl;
  }
  
  for (std::vector<struct s_nae*>::iterator nae = naConf->naelist.begin() ; nae != naConf->naelist.end(); ++nae) {
    // New NAE process configured from naConf->naelist data
    NAE* n_nae = newNAE(*nae);
    // New NAE added to vector NAEs
    NAEs.push_back(n_nae);
    // New NAE added to vector ioJack nae_channels
    naJack->addNaeChannel(n_nae);
    found_left = false;
    found_right = false;
    // Connecting jackaudio input to NAE inputs following configuration (naConf)
    for(std::vector<struct jackport*>::iterator it = naConf->jackclient->inports.begin() ; it != naConf->jackclient->inports.end(); ++it) {
      if((*nae)->left_in == (*it)->name) {
	if(!quiet) 
	  std::cout << "NatAmbio: creating jack input " << (*it)->name << " connection to NAE " << (*nae)->name << ":" << (*nae)->left_in << std::endl;
	found_left = true;
	naJack->connectInputNaePort(LEFT, (*it)->name, n_nae);
      }
      else if((*nae)->right_in == (*it)->name) {
	if(!quiet) 
	  std::cout << "NatAmbio: creating jack input " << (*it)->name << " connection to NAE " << (*nae)->name << ":" << (*nae)->right_in << std::endl;
	found_right = true;
	naJack->connectInputNaePort(RIGHT, (*it)->name, n_nae);
      }
    }
    if(!found_left) {
      throw std::runtime_error("NatAmbio: NAE " + (*nae)->name + " jack input port not found:" + (*nae)->left_in + "\n");
      return false;
    }
    if(!found_right) {
      throw std::runtime_error("NatAmbio: NAE " + (*nae)->name + " jack input port not found:" + (*nae)->right_in + "\n");
      return false;
    }
  }
  for(std::vector<struct convol*>::iterator cv = naConf->convollist.begin(); cv != naConf->convollist.end(); ++cv) {
    for(std::vector<string>::iterator naeinp = (*cv)->from_nae.begin(); naeinp != (*cv)->from_nae.end(); ++naeinp) {
      found_nae = false;
      for (std::vector<NAE*>::iterator nae = this->NAEs.begin() ; nae != this->NAEs.end(); ++nae) {
   	if((*naeinp) == (*nae)->getChannelOut(LEFT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(LEFT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(LEFT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(LEFT, *nae);
	    }
	  }
	}
	if((*naeinp) == (*nae)->getChannelOut(RIGHT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(RIGHT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(RIGHT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(RIGHT, *nae);
	    }
	  }
	}
	if((*naeinp) == (*nae)->getChannelOut(MID_LEFT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(MID_LEFT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(MID_LEFT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(MID_LEFT, *nae);
	    }
	  }
	}
	if((*naeinp) == (*nae)->getChannelOut(MID_RIGHT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(MID_RIGHT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(MID_RIGHT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(MID_RIGHT, *nae);
	    }
	  }
	}
	if((*naeinp) == (*nae)->getChannelOut(SIDE_LEFT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(SIDE_LEFT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(SIDE_LEFT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(SIDE_LEFT, *nae);
	    }
	  }
	}
	if((*naeinp) == (*nae)->getChannelOut(SIDE_RIGHT)) {
	  if(!quiet) 
	    std::cout << "NatAmbio: creating NAE input to convproc " << (*cv)->name << " from NAE " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(SIDE_RIGHT) << std::endl;
	  found_nae = true;
	  for (std::vector<ConvChannel*>::iterator ch = convChannels.begin() ; ch != convChannels.end(); ++ch) {
	    if((*ch)->get_name() == (*cv)->name) {
	      if(!quiet) 
		std::cout << "NatAmbio: connecting NAE output " << (*nae)->getName() << " output channel " << (*nae)->getChannelOut(SIDE_RIGHT) << " to convChannel " << (*ch)->get_name() << std::endl;
	      (*ch)->addNaeInput(SIDE_RIGHT, *nae);
	    }
	  }
	}
      }
      if(!found_nae) {
	throw std::runtime_error("NatAmbio: convolver " + (*cv)->name + " input NAE not found:"+ (*naeinp) + "\n");
	return false;
      }
    }
  }
  if(!quiet) {
    std::cout << "NatAmbio: number of NAE processes " << NAEs.size() << std::endl;
  }
  for (std::vector<NAE*>::iterator pn = NAEs.begin() ; pn != NAEs.end(); ++pn) {
    (*pn)->load(naJack->getPriority(), naJack->getPolicy());
  }
  return true;
}

NAE *NatAmbio::newNAE(struct s_nae* n_nae)
{
  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "NatAmbio: new NAE process creation " << std::endl;
    std::cout << "NatAmbio: NAE name " << n_nae->name << std::endl;
    std::cout << "NatAmbio: NAE mode " << n_nae->mode << std::endl;
    if(n_nae->mode) {
      std::cout << "NatAmbio: NAE surround gain " << n_nae->gain_surr << std::endl;
    } else {
      std::cout << "NatAmbio: NAE main gain " << n_nae->gain_main << std::endl;
      std::cout << "NatAmbio: NAE ambience gain " << n_nae->gain_amb << std::endl;
    }
  }
  NAE *n_nae_p;
  n_nae_p = new NAE(n_nae->name, n_nae->mode);
  n_nae_p->setMainGain(n_nae->gain_main);
  n_nae_p->setAmbGain(n_nae->gain_amb);
  n_nae_p->setSurrGain(n_nae->gain_surr);
  n_nae_p->setSampleCount(naJack->getPartSize());
  n_nae_p->setCovStepsLength(n_nae->steps_length);
  n_nae_p->setChannelIn(LEFT, n_nae->left_in);
  n_nae_p->setChannelIn(RIGHT, n_nae->right_in);
  if(!n_nae->left_out.empty())
    n_nae_p->setChannelOut(LEFT, n_nae->left_out);
  if(!n_nae->right_out.empty())
    n_nae_p->setChannelOut(RIGHT, n_nae->right_out);
  if(!n_nae->mid_left_out.empty())
    n_nae_p->setChannelOut(MID_LEFT, n_nae->mid_left_out);
  if(!n_nae->mid_right_out.empty())
    n_nae_p->setChannelOut(MID_RIGHT, n_nae->mid_right_out);
  if(!n_nae->side_left_out.empty())
    n_nae_p->setChannelOut(SIDE_LEFT, n_nae->side_left_out);
  if(!n_nae->side_right_out.empty())
    n_nae_p->setChannelOut(SIDE_RIGHT, n_nae->side_right_out);  
  // Connecting jackaudio input to nae inputs following configuration (naConf)
  for(std::vector<struct jackport*>::iterator it = naConf->jackclient->outports.begin() ; it != naConf->jackclient->outports.end(); ++it) {
    if(n_nae->left_out == (*it)->name) {
      if(!quiet) 
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->left_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(LEFT, (*it)->name, n_nae_p);
    }
    if(n_nae->right_out == (*it)->name) {
      if(!quiet)
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->right_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(RIGHT, (*it)->name, n_nae_p);
    }
    if(n_nae->mid_left_out == (*it)->name) {
      if(!quiet) 
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->mid_left_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(MID_LEFT, (*it)->name, n_nae_p);
    }
    if(n_nae->mid_right_out == (*it)->name) {
      if(!quiet)
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->mid_right_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(MID_RIGHT, (*it)->name, n_nae_p);
    }
    if(n_nae->side_left_out == (*it)->name) {
      if(!quiet) 
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->side_left_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(SIDE_LEFT, (*it)->name, n_nae_p);
    }
    if(n_nae->side_right_out == (*it)->name) {
      if(!quiet)
	std::cout << "NatAmbio: creating NAE output " << n_nae->name << ":" << n_nae->side_right_out << " connection to jack input " << (*it)->name <<  std::endl;
      naJack->connectOutputNaePort(SIDE_RIGHT, (*it)->name, n_nae_p);
    }
  }
  return n_nae_p;
}

void NatAmbio::connectPorts(void)
{
  for(std::vector<struct jackport*>::iterator it = naConf->jackclient->inports.begin() ; it != naConf->jackclient->inports.end(); ++it) {
    if(!quiet) {
      
      cout << "NatAmbio: connecting input port " << (*it)->name << " to " << (*it)->destname << endl;
    }
    naJack->ConnectInputPort((*it)->name, (*it)->destname);
  }
  for(std::vector<struct jackport*>::iterator it = naConf->jackclient->outports.begin() ; it != naConf->jackclient->outports.end(); ++it) {
    if(!quiet) 
      cout << "NatAmbio: connecting output port " << (*it)->name << " to " << (*it)->destname << endl;
    naJack->ConnectOutputPort((*it)->name, (*it)->destname);
  }
}

bool NatAmbio::convprocCheckStop(void)
{
  if(convChannels.empty())
    return false;
  // The convolver leaves ST_PROC only when it self-stops on sustained overload
  // (Convproc::process() calls stop_process() after repeated late partitions).
  // Don't poll check_stop() here: it returns true whenever all levels are idle
  // -- the normal state for filters that need no background worker threads --
  // and it also has the side effect of halting the convproc.
  return convproc->state() != Convproc::ST_PROC;
}

NatAmbio::~NatAmbio(void)
{
  // Tear down JACK first: deleting naJack deactivates and closes the client
  // (ioJack::synch_stop()), which guarantees the RT process callback has stopped
  // and won't run again. Only then is it safe to free the ConvChannels and the
  // Convproc the callback was using -- otherwise the callback dereferences freed
  // objects (use-after-free / heap corruption) during shutdown.
  delete naJack;
  naJack = NULL;
  for(std::vector<ConvChannel*>::iterator convc = convChannels.begin(); convc != convChannels.end(); convc++)
    delete *convc;
  convChannels.clear();
  delete convproc;
  delete naConf;
}
