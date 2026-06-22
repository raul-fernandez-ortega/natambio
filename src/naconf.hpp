/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _NA_NACONF_HPP_
#define _NA_NACONF_HPP_

#include "structs.hpp"

#ifdef __cplusplus
extern "C" {
#endif 

#include <limits.h>
#include <sndfile.h>

#ifdef __cplusplus
}
#endif

#include <math.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <iostream>

using namespace std;

void parse_error(const char msg[]);

void parse_error_exit(const char msg[]);

class NaConf {

private:

  bool quiet;
  int n_convprocs;
  int jack_sample_rate;   // JACK sample rate, probed before parsing (see NatAmbio)

  struct coeff* parse_coeff(xmlNodePtr xmlnode);
  struct xtc* parse_xtc(xmlNodePtr xmlnode);
  struct lowhigh* parse_lowhigh(xmlNodePtr xmlnode);
  struct loudness* parse_loudness(xmlNodePtr xmlnode);
  struct convol* parse_convol(xmlNodePtr xmlnode);
  //bool parse_panambio_old(xmlNodePtr xmlnode);
  struct s_nae* parse_nae(xmlNodePtr xmlnode);
  bool parse_jackinput(xmlNodePtr xmlnode);
  bool parse_jackoutput(xmlNodePtr xmlnode);
  bool parse_setting(xmlNodePtr xmlnode);
  bool sndfile_read(struct coeff* coeff);
  struct coeff* find_coeff(string name);
  bool build_convol_coeffs(void);
  bool build_xtc_coeffs(void);
  bool build_lowhigh_coeffs(void);
  bool build_loudness_coeffs(void);

public:

  vector<struct coeff*> coefslist;
  vector<struct xtc*> xtclist;
  vector<struct lowhigh*> lowhighlist;
  vector<struct loudness*> loudnesslist;
  vector<struct convol*> convollist;
  vector<struct s_nae*> naelist;
  struct jackclient* jackclient;
  
  NaConf(void);
  ~NaConf(void);

  void setQuiet(void) { quiet = true; };
  bool conf_init(string filename, int jack_sample_rate);
  int getMaxCoeffsSize(void);

};

#endif
