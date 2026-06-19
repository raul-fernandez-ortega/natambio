/*
 * (c) Copyright 2024/2025 -- Raul Fernandez
 *
 * This program is open source. For license terms, see the LICENSE file.
 *
 */

#include <string.h>
#include <pwd.h>

#include "naconf.hpp"

extern "C" {
#include "dsp.h"
#include "xtc.h"
#include "loudness.h"
}

#define PATH_SEPARATOR_CHAR '/'
#define PATH_SEPARATOR_STR "/"
#define CONVOLVER_NEEDS_CONFIGFILE 1

#define MINFILTERLEN 4
#define MAXFILTERLEN (1 << 30)

void parse_error(const char msg[])
{
  cerr << "Parsing error:" << std::endl;
  throw std::runtime_error(msg);
}

void parse_error_exit(const char msg[])
{
  cerr << "Parsing error:" << std::endl;
  throw std::runtime_error(msg);
  exit(0);
}

NaConf::NaConf(void)
{
  quiet = false;
  jackclient = new struct jackclient;
  jackclient->name = "natambio";
  n_convprocs = 0;
  jack_sample_rate = 0;
}

NaConf::~NaConf(void)
{
  for (vector<struct jackport*>::iterator jack_p = jackclient->inports.begin() ; jack_p != jackclient->inports.end(); jack_p++)
    delete *jack_p;
  for (vector<struct jackport*>::iterator jack_p = jackclient->outports.begin() ; jack_p != jackclient->outports.end(); jack_p++)
    delete *jack_p;
  delete(jackclient);
  for (vector<struct coeff*>::iterator coeff_p = coefslist.begin() ; coeff_p != coefslist.end(); coeff_p++) {
    free((*coeff_p)->coeffs);
    delete *coeff_p;
  }
  coefslist.clear();
  for (vector<struct convol*>::iterator convol_p = convollist.begin() ; convol_p != convollist.end(); convol_p++)
    delete *convol_p;
  convollist.clear();
  for (vector<struct s_nae*>::iterator nae_p = naelist.begin() ; nae_p != naelist.end(); nae_p++)
    delete *nae_p;
  naelist.clear();
  for (vector<struct xtc*>::iterator xtc_p = xtclist.begin() ; xtc_p != xtclist.end(); xtc_p++)
    delete *xtc_p;
  xtclist.clear();
  for (vector<struct lowhigh*>::iterator lh_p = lowhighlist.begin() ; lh_p != lowhighlist.end(); lh_p++)
    delete *lh_p;
  lowhighlist.clear();
  for (vector<struct loudness*>::iterator ld_p = loudnesslist.begin() ; ld_p != loudnesslist.end(); ld_p++)
    delete *ld_p;
  loudnesslist.clear();
}

struct coeff* NaConf::parse_coeff(xmlNodePtr xmlnode)
{
  char msg[200];
  struct coeff *coeff;
  string filename;
  size_t slashpos;
  char *homedir = NULL;
  
  homedir = getenv("HOME");
  
  coeff = new struct coeff;
  coeff->name = "";
  coeff->filename = "";
  coeff->channel = 0;
  coeff->skip = 0;
  coeff->length = 0;
  coeff->scale = 1;
  coeff->coeffs = NULL;
  coeff->convol_coeffs.clear();
  bool length_defined = false;

  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"name")) {
      coeff->name = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"gain")) {
      coeff->scale = FROM_DB(strtof((char*)cnt, NULL));
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"filename")) {
      coeff->filename = (char*)cnt;
      slashpos = coeff->filename.find("~");
      if(slashpos != string::npos)
	coeff->filename.replace(slashpos,1,homedir);
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"channel")) {
      coeff->channel = strtol((char*)cnt, NULL, 10) - 1;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"length")) {
      coeff->length = (sf_count_t) strtol((char*)cnt, NULL, 10);
      length_defined = true;
    }  else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"skip")) {
      coeff->skip = strtol((char*)cnt, NULL, 10);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"convol_coeff")) {
      coeff->convol_coeffs.push_back((char*)cnt);
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }
  if(coeff->name.empty()) {
    parse_error("Error: coef name not defined.");
    delete coeff;
    return NULL;
  }
  if(coeff->convol_coeffs.empty()) {
    // Plain coeff: its samples come from an audio file, read it now.
    if(coeff->filename.empty()) {
      sprintf(msg,"Error: coef filename not defined for coeff %s",coeff->name.c_str());
      parse_error(msg);
      delete coeff;
      return NULL;
    }
    // When <length> is not given, read the whole file: set length to the file's
    // total frame count, or that count minus <skip> when skipping leading frames.
    if(!length_defined) {
      SF_INFO probe;
      memset(&probe, 0, sizeof(probe));
      SNDFILE *pf = sf_open(coeff->filename.c_str(), SFM_READ, &probe);
      if(pf != NULL) {
	coeff->length = (coeff->skip > 0) ? probe.frames - coeff->skip : probe.frames;
	if(coeff->length < 0)
	  coeff->length = 0;
	sf_close(pf);
	if(!quiet)
	  std::cout << "Coeff '" << coeff->name << "': <length> not set, using full file ("
		    << coeff->length << " samples"
		    << (coeff->skip > 0 ? ", skip subtracted" : "") << ")." << std::endl;
      }
      // If the probe fails the file is likely missing; sndfile_read() reports it.
    }
    if(!(sndfile_read(coeff))) {
      delete coeff;
      return NULL;
    }
  } else {
    // Derived coeff: built later in build_convol_coeffs() from <convol_coeff>s.
    // When <length> is not given, assume the full linear-convolution length now:
    // convolving coeffs of lengths L0..L(n-1) yields sum(Li) - (n-1) samples.
    // This a-priori calc only works when every referenced coeff is already
    // declared with a known length; coeffs generated after parsing (XTC,
    // low_and_high_filter, loudness) are not in coefslist yet, so if any
    // reference is unresolved length is left 0 and build_convol_coeffs() sets it
    // to the full convolution length once all coeffs exist.
    if(!length_defined) {
      sf_count_t full = 0;
      bool resolvable = true;
      for (vector<string>::iterator nm = coeff->convol_coeffs.begin();
	   nm != coeff->convol_coeffs.end(); ++nm) {
	struct coeff *src = find_coeff(*nm);
	if (src == NULL || src->length <= 0) { resolvable = false; break; }
	full += src->length;
      }
      if (resolvable)
	coeff->length = full - (sf_count_t)(coeff->convol_coeffs.size() - 1);
    }
    if(!quiet) {
      std::cout << "Derived coeff '" << coeff->name << "' from "
		<< coeff->convol_coeffs.size() << " convol_coeff(s)";
      if(!length_defined && coeff->length > 0)
	std::cout << ", assumed full convolution length " << coeff->length << " samples";
      else if(!length_defined)
	std::cout << ", length resolved later (references a generated coeff)";
      std::cout << "." << std::endl;
    }
  }
  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "New coeff struct:" << std::endl;
    std::cout << "\tName: " << coeff->name << std::endl;
    std::cout << "\tFilename: " << coeff->filename << std::endl;
    std::cout << "\tScale factor: " << coeff->scale << std::endl;
    std::cout << "\tLenght: " << coeff->length << " samples" << std::endl;
    std::cout << "\tSkip: " << coeff->skip << " samples" << std::endl;
  }
  
  return coeff;
}

/* Parse a <xtc> block into a struct xtc. The two resulting filters are not
 * computed here; build_xtc_coeffs() runs process() (xtc.c) afterwards and
 * appends the direct/cross coeffs to coefslist. */
struct xtc* NaConf::parse_xtc(xmlNodePtr xmlnode)
{
  struct xtc *xtc = new struct xtc;
  xtc->direct_name = "";
  xtc->cross_name  = "";
  xtc->itd_us      = 0;
  xtc->ild_db      = 0.0;
  xtc->ild_alpha   = 0.0;
  xtc->azimuth_deg = 0;
  xtc->filter_len  = 0;

  // Every <xtc> parameter is mandatory; track which tags actually appear so a
  // value that happens to equal the zero default is not mistaken for "absent".
  bool has_itd = false, has_ild = false, has_alpha = false,
       has_azimuth = false, has_length = false;

  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"itd_us")) {
      xtc->itd_us = (int) strtol((char*)cnt, NULL, 10); has_itd = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"ild_db")) {
      xtc->ild_db = strtod((char*)cnt, NULL); has_ild = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"ild_alpha")) {
      xtc->ild_alpha = strtod((char*)cnt, NULL); has_alpha = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"azimuth_deg")) {
      xtc->azimuth_deg = (int) strtol((char*)cnt, NULL, 10); has_azimuth = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"length")) {
      xtc->filter_len = (int) strtol((char*)cnt, NULL, 10); has_length = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"direct_filter_name")) {
      xtc->direct_name = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"cross_filter_name")) {
      xtc->cross_name = (char*)cnt;
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }

  // All <xtc> parameters are required.
  const char *missing = NULL;
  if      (xtc->direct_name.empty()) missing = "direct_filter_name";
  else if (xtc->cross_name.empty())  missing = "cross_filter_name";
  else if (!has_itd)                  missing = "itd_us";
  else if (!has_ild)                  missing = "ild_db";
  else if (!has_alpha)                missing = "ild_alpha";
  else if (!has_azimuth)              missing = "azimuth_deg";
  else if (!has_length)               missing = "length";
  if (missing != NULL) {
    char msg[120];
    snprintf(msg, sizeof(msg), "Error: xtc <%s> is required but not defined.", missing);
    parse_error(msg);
    delete xtc;
    return NULL;
  }
  if(xtc->filter_len <= 0) {
    parse_error("Error: xtc <length> must be > 0.");
    delete xtc;
    return NULL;
  }

  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "New xtc struct:" << std::endl;
    std::cout << "\tDirect filter name: " << xtc->direct_name << std::endl;
    std::cout << "\tCross filter name: " << xtc->cross_name << std::endl;
    std::cout << "\tITD: " << xtc->itd_us << " us" << std::endl;
    std::cout << "\tILD: " << xtc->ild_db << " dB" << std::endl;
    std::cout << "\tILD alpha: " << xtc->ild_alpha << std::endl;
    std::cout << "\tAzimuth: " << xtc->azimuth_deg << " degrees" << std::endl;
    std::cout << "\tFilter length: " << xtc->filter_len << " samples" << std::endl;
  }

  return xtc;
}

/* Parse a <low_and_high_filter> block into a struct lowhigh. The two
 * complementary filters are not computed here; build_lowhigh_coeffs() runs
 * firwin2()/minimum_phase() (dsp.c) afterwards and appends the low-pass/
 * high-pass coeffs to coefslist. */
struct lowhigh* NaConf::parse_lowhigh(xmlNodePtr xmlnode)
{
  struct lowhigh *lh = new struct lowhigh;
  lh->low_name    = "";
  lh->high_name   = "";
  lh->frequency   = 0.0;
  lh->db_octave   = 0.0;
  lh->gain        = 0.0;   // optional; default 0 dB (unity pass-band)
  lh->filter_len  = 0;

  // All <low_and_high_filter> parameters are mandatory EXCEPT <gain> (optional,
  // default 0 dB); track tag presence so a value equal to the zero default is
  // not mistaken for "absent".
  bool has_freq = false, has_octave = false, has_length = false;

  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"frequency")) {
      lh->frequency = strtod((char*)cnt, NULL); has_freq = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"dB_octave")) {
      lh->db_octave = strtod((char*)cnt, NULL); has_octave = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"gain")) {
      lh->gain = strtod((char*)cnt, NULL);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"length")) {
      lh->filter_len = (int) strtol((char*)cnt, NULL, 10); has_length = true;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"low_pass_coeff_name")) {
      lh->low_name = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"high_pass_coeff_name")) {
      lh->high_name = (char*)cnt;
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }

  // All <low_and_high_filter> parameters are required except <gain> (optional).
  const char *missing = NULL;
  if      (lh->low_name.empty())  missing = "low_pass_coeff_name";
  else if (lh->high_name.empty()) missing = "high_pass_coeff_name";
  else if (!has_freq)             missing = "frequency";
  else if (!has_octave)           missing = "dB_octave";
  else if (!has_length)           missing = "length";
  if (missing != NULL) {
    char msg[140];
    snprintf(msg, sizeof(msg), "Error: low_and_high_filter <%s> is required but not defined.", missing);
    parse_error(msg);
    delete lh;
    return NULL;
  }
  if(lh->frequency <= 0.0) {
    parse_error("Error: low_and_high_filter <frequency> must be > 0.");
    delete lh;
    return NULL;
  }
  if(lh->filter_len <= 0) {
    parse_error("Error: low_and_high_filter <length> must be > 0.");
    delete lh;
    return NULL;
  }

  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "New low_and_high_filter struct:" << std::endl;
    std::cout << "\tLow pass filter name: " << lh->low_name << std::endl;
    std::cout << "\tHigh pass filter name: " << lh->high_name << std::endl;
    std::cout << "\tCrossover frequency: " << lh->frequency << " Hz" << std::endl;
    std::cout << "\tSlope: " << lh->db_octave << " dB/octave" << std::endl;
    std::cout << "\tGain: " << lh->gain << " dB" << std::endl;
    std::cout << "\tFilter length: " << lh->filter_len << " samples" << std::endl;
  }

  return lh;
}

/* Parse a <loudness> block into a struct loudness. The filter is not computed
 * here; build_loudness_coeffs() runs the equal-loudness model (loudness.c) plus
 * firwin2()/minimum_phase() (dsp.c) afterwards and appends the resulting coeff
 * to coefslist. */
struct loudness* NaConf::parse_loudness(xmlNodePtr xmlnode)
{
  struct loudness *ld = new struct loudness;
  ld->name        = "";
  ld->model       = "";
  ld->phon        = 0.0;
  ld->ref_phon    = 0.0;
  ld->filter_len  = 0;

  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"phon")) {
      ld->phon = strtod((char*)cnt, NULL);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"ref_phon")) {
      ld->ref_phon = strtod((char*)cnt, NULL);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"model")) {
      ld->model = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"length")) {
      ld->filter_len = (int) strtol((char*)cnt, NULL, 10);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"filter_name")) {
      ld->name = (char*)cnt;
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }

  if(ld->name.empty()) {
    parse_error("Error: loudness filter_name not defined.");
    delete ld;
    return NULL;
  }
  if(ld->model.empty()) {
    parse_error("Error: loudness model not defined.");
    delete ld;
    return NULL;
  }
  if(ld->filter_len <= 0) {
    parse_error("Error: loudness length not defined.");
    delete ld;
    return NULL;
  }

  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "New loudness struct:" << std::endl;
    std::cout << "\tFilter name: " << ld->name << std::endl;
    std::cout << "\tModel: " << ld->model << std::endl;
    std::cout << "\tPhon: " << ld->phon << std::endl;
    std::cout << "\tReference phon: " << ld->ref_phon << std::endl;
    std::cout << "\tFilter length: " << ld->filter_len << " samples" << std::endl;
  }

  return ld;
}

struct convol* NaConf::parse_convol(xmlNodePtr xmlnode)
{
  struct convol *naconvol = new convol;
  // Defaults for optional tags (otherwise these stay uninitialised when <delay>
  // / <gain> are absent: a garbage delay later mis-sizes the output ring
  // buffer in ConvChannel::set_delay() and crashes the RT callback).
  naconvol->delay = 0;
  naconvol->scale = 1;

  do {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"name")) {
      naconvol->name = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"coeff_name")) {
      naconvol->coeff_name = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"from_input")) {
      naconvol->from_inputs.push_back((char*)cnt);
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"to_output")) {
      naconvol->to_outputs.push_back((char*)cnt);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"from_convol")) {
      naconvol->from_convols.push_back((char*)cnt);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"from_nae")) {
      naconvol->from_nae.push_back((char*)cnt);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"delay")) {
      naconvol->delay = (int) strtol((char*)cnt, NULL, 10);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"gain")) {
      naconvol->scale = FROM_DB(strtof((char*)cnt, NULL));
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  } while(xmlnode != NULL);

  if(naconvol->name.empty()) {
    parse_error("Error: convolver process name not defined.");
    delete naconvol;
    return NULL;
  }
  if(naconvol->coeff_name.empty()) {
    parse_error_exit("Error: convolver process coef name not defined.");
    delete naconvol;
    return NULL;
  }

  naconvol->index = n_convprocs;
  n_convprocs++;

  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "New convolver process struct:" << std::endl;
    std::cout << "\tIndex:" << naconvol->index << std::endl;
    std::cout << "\tName: " << naconvol->name << std::endl;
    std::cout << "\tGain (linear): " << naconvol->scale << std::endl;
    std::cout << "\tDelay: " << naconvol->delay << std::endl;
    std::cout << "\tCoeff name: " << naconvol->coeff_name << std::endl;
    std::cout << "\tFrom inputs: " << std::endl;
    for (std::vector<string>::iterator it = naconvol->from_inputs.begin() ; it != naconvol->from_inputs.end(); ++it)
      std::cout << "\t\t" << (*it) << std::endl;
    std::cout << "\tFrom convols: " << std::endl;
    for (std::vector<string>::iterator it = naconvol->from_convols.begin() ; it != naconvol->from_convols.end(); ++it)
      std::cout << "\t\t" << (*it) << std::endl;
    std::cout << "\tFrom NAE: " << std::endl;
    for (std::vector<string>::iterator it = naconvol->from_nae.begin() ; it != naconvol->from_nae.end(); ++it)
      std::cout << "\t\t" << (*it) << std::endl;  
    std::cout << "\tTo outputs: " << std::endl;
    for (std::vector<string>::iterator it = naconvol->to_outputs.begin() ; it != naconvol->to_outputs.end(); ++it)
      std::cout << "\t\t" << (*it) << std::endl;

  }
  
  return naconvol;
}

/*bool NaConf::parse_panambio_old(xmlNodePtr xmlnode)
{ 
  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"pca")) {
      this->naelist.push_back(parse_pca(xmlnode->children));
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }
  if(!quiet) {
    for (std::vector<nae*>::iterator it = this->naelist.begin() ; it != this->naelist.end(); ++it) {
      std::cout << std::fixed << std::setprecision(3);
      std::cout << "New NAE struct:" << std::endl;
      std::cout << "\tName: " << (*it)->name << std::endl;
      std::cout << "\tLeft channel input: " << (*it)->left_in << std::endl;
      std::cout << "\tRight channel input: " << (*it)->right_in << std::endl;
      std::cout << "\tMode: " << (*it)->mode << std::endl;
      if((*it)->mode) {
	std::cout << "\t\tRear surround gain: " << (*it)->gain_surr << std::endl;
      } else {
	std::cout << "\t\tFront main gain: " << (*it)->gain_main << std::endl;
	std::cout << "\t\tFront ambience gain: " << (*it)->gain_amb << std::endl;
      }
      std::cout << "\tLeft channel output: " << (*it)->left_out << std::endl;
      std::cout << "\tRight channel output: " << (*it)->right_out << std::endl;
    }
  }
  return true;
} */
	
struct s_nae* NaConf::parse_nae(xmlNodePtr xmlnode)
{
  struct s_nae *nae;
  string mode;
  
  nae = new struct s_nae;
  nae->name = "";
  nae->mode = -1;
  nae->gain_main = 0;
  nae->gain_amb = 0;
  nae->gain_surr = 0;
  nae->steps_length = 5;   // optional; default 5 (PCA / covariance window, in blocks)
  nae->left_in = "";
  nae->right_in = "";
  nae->left_out = "";
  nae->right_out = "";
  
  while (xmlnode != NULL) {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"name")) {
      nae->name = (char*)cnt;
    } if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"steps_length")) {
      nae->steps_length = (int) strtol((char*) cnt, NULL, 10);
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"mode")) {
      mode = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"front_gain")) {
      nae->gain_main = FROM_DB(strtof((char*)cnt, NULL));
    }  else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"ambience_gain")) {
      nae->gain_amb = FROM_DB(strtof((char*)cnt, NULL));
    }  else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"rear_gain")) {
      nae->gain_surr = FROM_DB(strtof((char*)cnt, NULL));
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"input_left")) {
      nae->left_in = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"input_right")) {
      nae->right_in = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"output_left")) {
      nae->left_out = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"output_right")) {
      nae->right_out = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"front_output_left")) {
      nae->mid_left_out = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"front_output_right")) {
      nae->mid_right_out = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"amb_output_left")) {
      nae->side_left_out = (char*)cnt;
    } else if  (!xmlStrcmp(xmlnode->name, (const xmlChar *)"amb_output_right")) {
      nae->side_right_out = (char*)cnt;
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  }
  if(nae->left_in.empty()) {
    parse_error("Error: nae left_channel not defined.");
    this->naelist.clear();
    return NULL;
  }
  if(nae->right_in.empty()) {
    parse_error("Error: nae right_channel not defined.");
    this->naelist.clear();
    return NULL;
  }
  if(nae->steps_length < 1) {
    parse_error("Error: nae steps_length must be >= 1.");
    this->naelist.clear();
    return NULL;
  }
  if(mode == "alpha") {
    nae->mode = 0;
    if(nae->gain_main == 0) {
      parse_error("Error: nae alpha mode front_gain not defined.");
      delete nae;
      return NULL;
    }
    if(nae->gain_amb == 0) {
      parse_error("Error: nae alpha mode ambience_gain not defined.");
      delete nae;
      return NULL;
    }
  } else if(mode == "beta") {
    nae->mode = 1;
    if(nae->gain_surr == 0) {
      parse_error("Error: nae beta mode rear_gain not defined.");
      delete nae;
      return NULL;
    }
  } else {
    parse_error("Error: nae process mode not defined.");
    delete nae;
    return NULL;
  }
  if(nae->left_out.empty() && nae->mid_left_out.empty() && nae->side_left_out.empty()) {
    parse_error("Error: nae left output not defined.");
    delete nae;
    return NULL;
  }
  if(nae->right_out.empty() && nae->mid_right_out.empty() && nae->side_right_out.empty()) {
    parse_error("Error: nae right output not defined.");
    delete nae;
    return NULL;
  }
  std::cout << std::fixed << std::setprecision(3);
  std::cout << "New NAE struct:" << std::endl;
  std::cout << "\tName: " << nae->name << std::endl;
  std::cout << "\tLeft channel input: " << nae->left_in << std::endl;
  std::cout << "\tRight channel input: " << nae->right_in << std::endl;
  std::cout << "\tMode: " << nae->mode << std::endl;
  if(nae->mode) {
    std::cout << "\t\tRear surround gain: " << nae->gain_surr << std::endl;
  } else {
    std::cout << "\t\tFront main gain: " << nae->gain_main << std::endl;
    std::cout << "\t\tFront ambience gain: " << nae->gain_amb << std::endl;
  }
  if(!nae->left_out.empty())
    std::cout << "\tLeft channel output: " << nae->left_out << std::endl;
  if(!nae->right_out.empty())
    std::cout << "\tRight channel output: " << nae->right_out << std::endl;
  if(!nae->mid_left_out.empty())
    std::cout << "\tMid left channel output: " << nae->mid_left_out << std::endl;
  if(!nae->mid_right_out.empty())
    std::cout << "\tMid right channel output: " << nae->mid_right_out << std::endl;
  if(!nae->side_left_out.empty())
    std::cout << "\tSide left channel output: " << nae->side_left_out << std::endl;
  if(!nae->side_right_out.empty())
    std::cout << "\tSide right channel output: " << nae->side_right_out << std::endl;
  return nae;
}

bool NaConf::parse_jackinput(xmlNodePtr xmlnode)
{
  int i = 1;
  xmlNodePtr xmlchild;
  struct jackport* newjackport;
  
  do {
    xmlChar *cnt = xmlNodeGetContent(xmlnode);
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"clientname")) {
      this->jackclient->name = (char*)cnt;
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"port")) {
      newjackport = new jackport {};
      xmlchild = xmlnode->children;
      newjackport->name.clear();
      newjackport->destname.clear();
      while(xmlchild != NULL) {
	xmlChar *cnt2 = xmlNodeGetContent(xmlchild);
	if (!xmlStrcmp(xmlchild->name, (const xmlChar *)"name")) {
	  newjackport->name = (char*)cnt2;
	} else if (!xmlStrcmp(xmlchild->name, (const xmlChar *)"destname")) {
	  newjackport->destname = (char*)cnt2;
	}
	xmlFree(cnt2);
	xmlchild = xmlchild->next;
      }
      this->jackclient->inports.push_back(newjackport);
    }
    xmlFree(cnt);
    xmlnode = xmlnode->next;
  } while(xmlnode != NULL);

  if(this->jackclient->name.empty()) {
    parse_error("Error: jack client name not defined.");
    return false;
  }
  if(this->jackclient->inports.size() == 0) {
    parse_error("No input channels for jack client");
    return false;
  }
  
  if(!quiet) {
    std::cout << fixed << setprecision(3);
    std::cout << "Jackclient struct:" << std::endl;
    std::cout << "\tName: " << this->jackclient->name << std::endl;
    std::cout << "\tJack inputs: " << std::endl;
    for (std::vector<struct jackport*>::iterator it = this->jackclient->inports.begin() ; it != this->jackclient->inports.end(); ++it) {
      std::cout << "\t\tInput no." << i << std::endl;
      std::cout << "\t\t\tInput name:" << (*it)->name << std::endl;
      std::cout << "\t\t\tInput destination:" << (*it)->destname << std::endl;
      i++;
    }
  }
  
  return true;
}

bool NaConf::parse_jackoutput(xmlNodePtr xmlnode)
{
  int i = 1;
  xmlNodePtr xmlchild;
  struct jackport* newjackport;
  
  do {
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"port")) {
      newjackport = new jackport {};
      xmlchild = xmlnode->children;
      newjackport->name.clear();
      newjackport->destname.clear();
      while(xmlchild != NULL) {
	xmlChar *cnt = xmlNodeGetContent(xmlchild);
	if (!xmlStrcmp(xmlchild->name, (const xmlChar *)"name")) {
	  newjackport->name = (char*)cnt;
	} else if (!xmlStrcmp(xmlchild->name, (const xmlChar *)"destname")) {
	  newjackport->destname = (char*)cnt;
	}
	xmlFree(cnt);
	xmlchild = xmlchild->next;
      }
      this->jackclient->outports.push_back(newjackport);
    }
    xmlnode = xmlnode->next;
  } while(xmlnode != NULL);

  if(this->jackclient->outports.size() == 0) {
    parse_error("No output channels for jack client");
    return false;
  }
  
  if(!quiet) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Jackclient struct:" << std::endl;
    std::cout << "\tJack outputs: " << std::endl;
    for (std::vector<struct jackport*>::iterator it = this->jackclient->outports.begin() ; it != this->jackclient->outports.end(); ++it) {
      std::cout << "\t\tOutput no." << i << std::endl;
      std::cout << "\t\t\tOutput name:" << (*it)->name << std::endl;
      std::cout << "\t\t\tOutput destination:" << (*it)->destname << std::endl;
      i++;
    }
  }
  
  return true;
}

bool NaConf::sndfile_read(struct coeff *coeff)
{
  SNDFILE* infile;
  sf_count_t n_items = 0;
  int i, j;
  float *fbuffer;
  int totitems;
  
  if ((infile = sf_open(coeff->filename.c_str(), SFM_READ, &(coeff->snfinfo))) == NULL) {
    std::cerr << "coeffs file not found..." << coeff->filename << std::endl;
    return false;
  }
  // The impulse response must be at the JACK sample rate (probed before parsing).
  if(coeff->snfinfo.samplerate != jack_sample_rate) {
    std::cerr << "Error: coeff '" << coeff->name << "' file sample rate ("
	      << coeff->snfinfo.samplerate << " Hz) differs from the JACK sample rate ("
	      << jack_sample_rate << " Hz): " << coeff->filename << std::endl;
    sf_close(infile);
    return false;
  }
  if(!quiet) {
    std::cout << "Coeff file: " << coeff->filename << std::endl;
    std::cout << "\tData:" << std::endl;
    std::cout << "\t\tSample Rate: " << coeff->snfinfo.samplerate << std::endl;
    std::cout << "\t\tNumber of channels: " << coeff->snfinfo.channels << std::endl;
    std::cout << "\t\tNumber of frames: " << coeff->snfinfo.frames << std::endl;
  }

  fbuffer = (float*) malloc(coeff->snfinfo.channels * coeff->snfinfo.frames * sizeof(float));
  if (fbuffer == NULL) {
    std::cerr << "Could not allocate memory for sndfile data\n" << std::endl;
    sf_close(infile);
    return false;
  }
  if (coeff->skip > 0 && sf_seek(infile, coeff->skip, SEEK_SET) != coeff->skip) {
    std::cerr << "Could not skip " << coeff->skip << "frames at " << coeff->filename << std::endl;
    free(fbuffer);
    sf_close(infile);
    return false;
  }
  if(coeff->length > 0) {
    if ((n_items = sf_readf_float(infile, fbuffer, coeff->length)) < coeff->length) {
      std::cerr << "Did not read enough frames from " << coeff->filename << ": "<< n_items << " instead of " << coeff->length << std::endl;
      sf_close(infile);
      free(fbuffer);
      return false;
    }
  } else {
    if((n_items = sf_readf_float(infile, fbuffer, coeff->snfinfo.frames - coeff->skip)) < coeff->snfinfo.frames - coeff->skip ) {
      std::cerr << "Error reading complete coeff file " << coeff->filename << ": "<< n_items << " samples instead of " << coeff->snfinfo.frames << std::endl;
      sf_close(infile);
      free(fbuffer);
      return false;
    }
    coeff->length = coeff->snfinfo.frames;
  }
  if(!quiet) {
    std::cout << "\tRead " << n_items << " coeffs." << std::endl;
  }
  
  totitems = n_items;
  if (coeff->length > 0 && totitems >= coeff->length)
    totitems = coeff->length;
  else
    coeff->length = n_items;

  if(!quiet) {
    std::cout << "\tCoeffs lenght " << coeff->length << " samples." << std::endl;
  }
  
  coeff->coeffs = (float*) malloc(totitems * sizeof(float));
  if (coeff->coeffs == NULL) {
    std::cerr << "Could not allocate memory for sndfile data" << std::endl;
    sf_close(infile);
    free(fbuffer);
    return false;
  }

  for (i = coeff->channel, j = 0; i < totitems * coeff->snfinfo.channels; i +=coeff->snfinfo.channels, j++)
    coeff->coeffs[j] = fbuffer[i]*coeff->scale;
  
  sf_close(infile);
  free(fbuffer);
  return true;
}


struct coeff* NaConf::find_coeff(string name)
{
  for (vector<struct coeff*>::iterator it = coefslist.begin(); it != coefslist.end(); ++it)
    if ((*it)->name == name)
      return *it;
  return NULL;
}

/* Allocate a coeff holding the given samples (converted double -> float) with a
 * known sample rate. Used by build_xtc_coeffs() for filters that are computed
 * in memory rather than read from a file, so snfinfo must be set explicitly for
 * the JACK sample-rate check in NatAmbio::startConvProc(). Returns NULL on OOM. */
static struct coeff* make_mem_coeff(string name, const double *data, int len, int samplerate)
{
  struct coeff *c = new struct coeff;
  c->name     = name;
  c->filename = "";
  c->channel  = 0;
  c->skip     = 0;
  c->length   = len;
  c->scale    = 1;
  c->convol_coeffs.clear();
  memset(&c->snfinfo, 0, sizeof(c->snfinfo));
  c->snfinfo.samplerate = samplerate;
  c->snfinfo.channels   = 1;
  c->snfinfo.frames     = len;
  c->coeffs = (float*) malloc((size_t) len * sizeof(float));
  if (c->coeffs == NULL) {
    delete c;
    return NULL;
  }
  for (int i = 0; i < len; i++)
    c->coeffs[i] = (float) data[i];
  return c;
}

/* Run the XTC filter generator (process() in xtc.c) for every <xtc> block and
 * append the resulting direct/cross coeffs to coefslist. Called before
 * build_convol_coeffs() so that derived coeffs may reference XTC outputs. */
bool NaConf::build_xtc_coeffs(void)
{
  char msg[300];

  for (vector<struct xtc*>::iterator it = xtclist.begin(); it != xtclist.end(); ++it) {
    struct xtc *r = *it;

    if (find_coeff(r->direct_name) != NULL || find_coeff(r->cross_name) != NULL) {
      snprintf(msg, sizeof(msg), "Error: xtc filter name '%s'/'%s' already used by another coeff.",
	       r->direct_name.c_str(), r->cross_name.c_str());
      parse_error(msg);
      return false;
    }

    double *direct = (double*) malloc((size_t) r->filter_len * sizeof(double));
    double *cross  = (double*) malloc((size_t) r->filter_len * sizeof(double));
    if (direct == NULL || cross == NULL) {
      free(direct); free(cross);
      parse_error("Error: could not allocate memory for xtc filters.");
      return false;
    }

    int rc = process(r->itd_us, r->ild_db, r->ild_alpha, r->azimuth_deg,
		     jack_sample_rate, r->filter_len, direct, cross);
    if (rc != 0) {
      free(direct); free(cross);
      snprintf(msg, sizeof(msg), "Error: xtc process() failed (rc=%d) for filters '%s'/'%s'.",
	       rc, r->direct_name.c_str(), r->cross_name.c_str());
      parse_error(msg);
      return false;
    }

    struct coeff *dc = make_mem_coeff(r->direct_name, direct, r->filter_len, jack_sample_rate);
    struct coeff *cc = make_mem_coeff(r->cross_name,  cross,  r->filter_len, jack_sample_rate);
    free(direct); free(cross);
    if (dc == NULL || cc == NULL) {
      if (dc) { free(dc->coeffs); delete dc; }
      if (cc) { free(cc->coeffs); delete cc; }
      parse_error("Error: could not allocate memory for xtc coeffs.");
      return false;
    }
    coefslist.push_back(dc);
    coefslist.push_back(cc);

    if (!quiet)
      std::cout << "Built xtc coeffs '" << r->direct_name << "' and '" << r->cross_name
		<< "' (" << r->filter_len << " samples each)." << std::endl;
  }

  return true;
}

/* firwin2 dB-magnitude model for the low-pass filter of a <low_and_high_filter>
 * block: a flat pass-band at the configured gain dB up to the crossover
 * frequency, then a constant db_octave dB/octave roll-off above it. Declared
 * with C language linkage so its pointer type matches firwin2_db_model_fn (dsp.h). */
extern "C" {
  typedef struct {
    double freq;         // crossover frequency, Hz
    double slope;        // roll-off slope above freq, dB/octave (positive)
    double passband_db;  // target pass-band gain in dB (= gain)
  } lowpass_ctx;

  static double lowpass_db_model(double f_hz, void *ctx_v) {
    const lowpass_ctx *c = (const lowpass_ctx *) ctx_v;
    if (f_hz <= c->freq)
      return c->passband_db;
    return c->passband_db - c->slope * log2(f_hz / c->freq);
  }
}

/* Generate the complementary low-pass / high-pass filter pair of every
 * <low_and_high_filter> block and append them to coefslist. The low-pass is a
 * linear-phase FIR designed by firwin2() against lowpass_db_model; the high-pass
 * is its complement, delta - low-pass (a linear-phase impulse, gain-
 * adjusted, centred on the filter), so that the two sum back to a gain-adjusted
 * delta. Both are then converted to minimum phase (dsp.c) -- those are the
 * coeffs that get applied. Runs before build_convol_coeffs() so derived coeffs
 * may reference the generated filters. */
bool NaConf::build_lowhigh_coeffs(void)
{
  char msg[300];

  for (vector<struct lowhigh*>::iterator it = lowhighlist.begin(); it != lowhighlist.end(); ++it) {
    struct lowhigh *lh = *it;

    if (find_coeff(lh->low_name) != NULL || find_coeff(lh->high_name) != NULL) {
      snprintf(msg, sizeof(msg), "Error: low_and_high_filter name '%s'/'%s' already used by another coeff.",
	       lh->low_name.c_str(), lh->high_name.c_str());
      parse_error(msg);
      return false;
    }

    int    n  = lh->filter_len;
    double *low_lin  = (double*) malloc((size_t) n * sizeof(double));
    double *high_lin = (double*) malloc((size_t) n * sizeof(double));
    double *low_min  = (double*) malloc((size_t) n * sizeof(double));
    double *high_min = (double*) malloc((size_t) n * sizeof(double));
    if (low_lin == NULL || high_lin == NULL || low_min == NULL || high_min == NULL) {
      free(low_lin); free(high_lin); free(low_min); free(high_min);
      parse_error("Error: could not allocate memory for low_and_high_filter filters.");
      return false;
    }

    // Linear-phase low-pass from the dB-magnitude model.
    lowpass_ctx ctx;
    ctx.freq        = lh->frequency;
    ctx.slope       = lh->db_octave;
    ctx.passband_db = lh->gain;
    int rc = firwin2(n, jack_sample_rate, lowpass_db_model, &ctx, low_lin);
    if (rc != 0) {
      free(low_lin); free(high_lin); free(low_min); free(high_min);
      snprintf(msg, sizeof(msg), "Error: firwin2 failed (rc=%d) for low_and_high_filter '%s'.",
	       rc, lh->low_name.c_str());
      parse_error(msg);
      return false;
    }

    // Complementary high-pass: a gain-adjusted, linear-phase delta
    // (centred at filter_len/2, matching the low-pass group delay) minus the
    // low-pass, so low + high == that gain-adjusted delta.
    double amp = FROM_DB(lh->gain);
    for (int i = 0; i < n; i++)
      high_lin[i] = -low_lin[i];
    high_lin[n / 2] += amp;

    // Convert both to minimum phase -- these are the applied filters.
    rc = minimum_phase(low_lin, n, low_min);
    if (rc != 0) {
      free(low_lin); free(high_lin); free(low_min); free(high_min);
      snprintf(msg, sizeof(msg), "Error: minimum_phase failed (rc=%d) for low_and_high_filter '%s'.",
	       rc, lh->low_name.c_str());
      parse_error(msg);
      return false;
    }
    rc = minimum_phase(high_lin, n, high_min);
    if (rc != 0) {
      free(low_lin); free(high_lin); free(low_min); free(high_min);
      snprintf(msg, sizeof(msg), "Error: minimum_phase failed (rc=%d) for low_and_high_filter '%s'.",
	       rc, lh->high_name.c_str());
      parse_error(msg);
      return false;
    }

    struct coeff *lc = make_mem_coeff(lh->low_name,  low_min,  n, jack_sample_rate);
    struct coeff *hc = make_mem_coeff(lh->high_name, high_min, n, jack_sample_rate);
    free(low_lin); free(high_lin); free(low_min); free(high_min);
    if (lc == NULL || hc == NULL) {
      if (lc) { free(lc->coeffs); delete lc; }
      if (hc) { free(hc->coeffs); delete hc; }
      parse_error("Error: could not allocate memory for low_and_high_filter coeffs.");
      return false;
    }
    coefslist.push_back(lc);
    coefslist.push_back(hc);

    if (!quiet)
      std::cout << "Built low/high filter coeffs '" << lh->low_name << "' and '" << lh->high_name
		<< "' (" << n << " samples each)." << std::endl;
  }

  return true;
}

/* Generate the loudness-compensation filter of every <loudness> block and
 * append it to coefslist. The equal-loudness *difference* curve (the model
 * contour at <phon> minus the contour at <ref_phon>, both normalised to 0 dB at
 * 1 kHz) is computed by loudness.c, turned into a linear-phase FIR with
 * firwin2() (sampling the curve through loudness_db_model), then converted to
 * minimum phase -- that minimum-phase filter is the coeff that gets applied.
 * Runs before build_convol_coeffs() so derived coeffs may reference it. */
bool NaConf::build_loudness_coeffs(void)
{
  char msg[300];

  for (vector<struct loudness*>::iterator it = loudnesslist.begin(); it != loudnesslist.end(); ++it) {
    struct loudness *ld = *it;

    if (find_coeff(ld->name) != NULL) {
      snprintf(msg, sizeof(msg), "Error: loudness filter name '%s' already used by another coeff.",
	       ld->name.c_str());
      parse_error(msg);
      return false;
    }

    // Equal-loudness difference curve (phon - ref_phon) on the model's grid.
    double *cfreq = NULL, *cdb = NULL;
    int cn = 0;
    int rc = loudness_diff_curve(ld->model.c_str(), ld->phon, ld->ref_phon, &cfreq, &cdb, &cn);
    if (rc != 0) {
      snprintf(msg, sizeof(msg), "Error: loudness model '%s' failed (rc=%d) for filter '%s'.",
	       ld->model.c_str(), rc, ld->name.c_str());
      parse_error(msg);
      return false;
    }

    int     n   = ld->filter_len;
    double *lin = (double*) malloc((size_t) n * sizeof(double));
    double *min = (double*) malloc((size_t) n * sizeof(double));
    if (lin == NULL || min == NULL) {
      free(lin); free(min); free(cfreq); free(cdb);
      parse_error("Error: could not allocate memory for loudness filter.");
      return false;
    }

    // Linear-phase FIR sampling the difference curve, then minimum phase.
    loudness_model_ctx ctx;
    ctx.freq = cfreq;
    ctx.db   = cdb;
    ctx.n    = cn;
    rc = firwin2(n, jack_sample_rate, loudness_db_model, &ctx, lin);
    free(cfreq); free(cdb);
    if (rc != 0) {
      free(lin); free(min);
      snprintf(msg, sizeof(msg), "Error: firwin2 failed (rc=%d) for loudness filter '%s'.",
	       rc, ld->name.c_str());
      parse_error(msg);
      return false;
    }
    rc = minimum_phase(lin, n, min);
    if (rc != 0) {
      free(lin); free(min);
      snprintf(msg, sizeof(msg), "Error: minimum_phase failed (rc=%d) for loudness filter '%s'.",
	       rc, ld->name.c_str());
      parse_error(msg);
      return false;
    }

    struct coeff *c = make_mem_coeff(ld->name, min, n, jack_sample_rate);
    free(lin); free(min);
    if (c == NULL) {
      parse_error("Error: could not allocate memory for loudness coeff.");
      return false;
    }
    coefslist.push_back(c);

    if (!quiet)
      std::cout << "Built loudness coeff '" << ld->name << "' (" << n << " samples, model "
		<< ld->model << ", " << ld->phon << "-" << ld->ref_phon << " phon)." << std::endl;
  }

  return true;
}

/* Build every derived coeff (those declared with one or more <convol_coeff>)
 * once the whole coeff list has been read. A single <convol_coeff> is copied
 * verbatim; several are convolved together (left to right) with
 * fft_convolve_truncate(). The result is stored, truncated/zero-padded to the
 * coeff's own <length> (or to the full convolution length when <length> is 0),
 * in coeff->coeffs. Referenced coeffs must be declared before the derived one. */
bool NaConf::build_convol_coeffs(void)
{
  char msg[300];

  for (vector<struct coeff*>::iterator it = coefslist.begin(); it != coefslist.end(); ++it) {
    struct coeff *dst = *it;
    if (dst->convol_coeffs.empty())
      continue;  // plain file-loaded coeff, nothing to build

    // Resolve and validate the referenced source coeffs. All sources must share
    // the same sample rate; the derived coeff inherits it (its snfinfo is not
    // filled by an audio file, so it must be set here for the JACK sample-rate
    // check in NatAmbio::startConvProc()).
    vector<struct coeff*> sources;
    int src_samplerate = 0;
    for (vector<string>::iterator nm = dst->convol_coeffs.begin(); nm != dst->convol_coeffs.end(); ++nm) {
      struct coeff *src = find_coeff(*nm);
      if (src == NULL) {
	snprintf(msg, sizeof(msg), "Error: convol_coeff '%s' of coeff '%s' not found.",
		 nm->c_str(), dst->name.c_str());
	parse_error(msg);
	return false;
      }
      if (src->coeffs == NULL || src->length <= 0) {
	snprintf(msg, sizeof(msg), "Error: convol_coeff '%s' of coeff '%s' has no data "
		 "(it must be declared before it).", nm->c_str(), dst->name.c_str());
	parse_error(msg);
	return false;
      }
      if (src_samplerate != 0 && src->snfinfo.samplerate != src_samplerate) {
	snprintf(msg, sizeof(msg), "Error: convol_coeff '%s' sample rate (%d) of coeff '%s' "
		 "differs from the other convol_coeffs (%d).",
		 nm->c_str(), src->snfinfo.samplerate, dst->name.c_str(), src_samplerate);
	parse_error(msg);
	return false;
      }
      src_samplerate = src->snfinfo.samplerate;
      sources.push_back(src);
    }

    // Convolve the sources together in double precision.
    int acc_len = (int) sources[0]->length;
    double *acc = (double*) malloc((size_t) acc_len * sizeof(double));
    if (acc == NULL) {
      parse_error("Error: could not allocate memory for coeff convolution.");
      return false;
    }
    for (int i = 0; i < acc_len; i++)
      acc[i] = (double) sources[0]->coeffs[i];

    for (size_t s = 1; s < sources.size(); s++) {
      int b_len   = (int) sources[s]->length;
      int out_len = acc_len + b_len - 1;
      double *b   = (double*) malloc((size_t) b_len * sizeof(double));
      double *out = (double*) malloc((size_t) out_len * sizeof(double));
      if (b == NULL || out == NULL) {
	free(acc); free(b); free(out);
	parse_error("Error: could not allocate memory for coeff convolution.");
	return false;
      }
      for (int i = 0; i < b_len; i++)
	b[i] = (double) sources[s]->coeffs[i];
      if (fft_convolve_truncate(acc, acc_len, b, b_len, out, out_len) != 0) {
	free(acc); free(b); free(out);
	snprintf(msg, sizeof(msg), "Error: fft_convolve_truncate failed building coeff '%s'.",
		 dst->name.c_str());
	parse_error(msg);
	return false;
      }
      free(acc);
      free(b);
      acc     = out;
      acc_len = out_len;
    }

    // Copy the result into the destination coeff, truncated/zero-padded to its length.
    int target_len = (dst->length > 0) ? (int) dst->length : acc_len;
    int copy_len   = MIN(target_len, acc_len);
    dst->coeffs = (float*) malloc((size_t) target_len * sizeof(float));
    if (dst->coeffs == NULL) {
      free(acc);
      parse_error("Error: could not allocate memory for derived coeff.");
      return false;
    }
    for (int i = 0; i < copy_len; i++)
      dst->coeffs[i] = (float) (acc[i] * dst->scale);
    for (int i = copy_len; i < target_len; i++)
      dst->coeffs[i] = 0.0f;
    dst->length = target_len;
    dst->snfinfo.samplerate = src_samplerate;  // inherit the sources' sample rate
    free(acc);

    if (!quiet)
      std::cout << "Built derived coeff '" << dst->name << "' (" << dst->length
		<< " samples) from " << dst->convol_coeffs.size() << " input coeff(s)." << std::endl;
  }

  return true;
}

bool NaConf::conf_init(string filename, int jack_sample_rate)
{
  struct coeff* n_coeff;
  struct xtc* n_xtc;
  struct lowhigh* n_lowhigh;
  struct loudness* n_loudness;
  struct convol* n_convol;
  struct s_nae* n_nae;
  xmlDocPtr xmlconf;
  xmlNodePtr xmlnode, xmlchildren, xmlnatambio;

  // JACK sample rate, probed before parsing: used to generate the xtc /
  // low_and_high_filter / loudness coeffs and to validate that every WAV
  // (<filename>) is at this rate.
  this->jack_sample_rate = jack_sample_rate;

  // Open document
  xmlconf = xmlReadFile(filename.c_str(), NULL, 0);
  if (xmlconf == NULL ) {
    std::cerr << "Document cannot be not parsed." << std::endl;
    return false;
  }
  
  xmlnode = xmlDocGetRootElement(xmlconf);
  if (xmlnode == NULL) {
    std::cerr << "Empty document found." << std::endl;
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }
  xmlnode = xmlnode->children;
  xmlnatambio = NULL;

  while (xmlnode !=NULL) {
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"natambio")) {
      if(!quiet) 
	std::cout << "<natambio> token found." << std::endl;
      xmlnatambio = xmlnode;
      break;
    } else {
      xmlchildren = xmlnode->children;
      while (xmlchildren != NULL) {
	if (!xmlStrcmp(xmlchildren->name, (const xmlChar *)"natambio")) {
	  if(!quiet)
	    std::cout << "<natambio> token found." << std::endl;
	  xmlnatambio = xmlchildren;
	  break;
	}	
	xmlchildren = xmlchildren->next;
      }
    }
    xmlnode = xmlnode->next;
  }
  if (xmlnatambio == NULL) {
    std::cerr << "<natambio> token not found." << std::endl;
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }
  xmlnode = xmlnatambio->children;
  while (xmlnode != NULL)  {
    if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"coeff")) {
      if((n_coeff = parse_coeff(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->coefslist.push_back(n_coeff);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"xtc")) {
      if((n_xtc = parse_xtc(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->xtclist.push_back(n_xtc);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"low_and_high_filter")) {
      if((n_lowhigh = parse_lowhigh(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->lowhighlist.push_back(n_lowhigh);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"loudness")) {
      if((n_loudness = parse_loudness(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->loudnesslist.push_back(n_loudness);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"convol")) {
      if((n_convol = parse_convol(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->convollist.push_back(n_convol);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"nae")) {
      if((n_nae = parse_nae(xmlnode->children))==NULL) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      } else
	this->naelist.push_back(n_nae);
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"jack_input")) {
      if(!(parse_jackinput(xmlnode->children))) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      }
    } else if (!xmlStrcmp(xmlnode->name, (const xmlChar *)"jack_output")) {
      if(!(parse_jackoutput(xmlnode->children))) {
	xmlCleanupParser();
	xmlFreeDoc(xmlconf);
	return false;
      }
    }
    xmlnode = xmlnode->next;
  }

  // Generate XTC filter coeffs (process() in xtc.c) before resolving derived
  // coeffs, so that <convol_coeff>s may reference the XTC direct/cross filters.
  if(!build_xtc_coeffs()) {
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }

  // Generate the low-pass/high-pass filter pairs (firwin2 + minimum_phase in
  // dsp.c), again before resolving derived coeffs so they may be referenced.
  if(!build_lowhigh_coeffs()) {
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }

  // Generate the loudness-compensation filters (equal-loudness model in
  // loudness.c + firwin2/minimum_phase in dsp.c), also before derived coeffs.
  if(!build_loudness_coeffs()) {
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }

  // All coeffs are read: now build the derived ones (<convol_coeff> convolutions).
  if(!build_convol_coeffs()) {
    xmlCleanupParser();
    xmlFreeDoc(xmlconf);
    return false;
  }

  xmlCleanupParser();
  xmlFreeDoc(xmlconf);
  return true;
}

int NaConf::getMaxCoeffsSize(void) {

  int max = 0;
  for(std::vector<struct coeff*>::iterator it = this->coefslist.begin() ; it != this->coefslist.end(); ++it) 
    if((*it)->length > max)
      max = (*it)->length;
  if(!quiet) 
    std::cout << "NaConf: Maximum coeffs size:" << max << std::endl;
  return max;
}
