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
#include <sstream>
#include <iostream>

using namespace std;

void parse_error(const char msg[]);

void parse_warning(const std::string& msg);
void parse_error_exit(const char msg[]);

/* How far down a generated FIR's template is taken literally, relative to the
   pass band. Below this the filter is only asked not to EXCEED it; see
   firwin2_deviation() in dsp.h for what the report does with it.

   It is the floor of the MEASUREMENT and not of the model. The two were one
   number at first, and that quietly cost 12 dB of stop-band depth: a 100 Hz
   crossover that used to reach -72 dB at 800 Hz stopped at -60, because the
   design was chasing the same floor the report was judging against. The design
   should go as deep as the length lets it; the report should stop caring long
   before that. */
#define NA_FIR_FLOOR_DB  (-60.0)

/* The floor of the MODEL, which is there only so that a skirt reaching zero
   frequency does not hand firwin2 a -inf to raise ten to. Deep enough to be
   out of the way of any length worth designing. */
#define NA_FIR_MODEL_FLOOR_DB (-200.0)

/* And the band it is compared over. Outside it the comparison is arithmetic
   rather than audible: a linear-phase FIR resolves nothing finer than about
   fs/length, so a 300 Hz skirt evaluated at 9 Hz reports a shortfall that no
   length within reason would fix and that nothing in the system reproduces. */
#define NA_FIR_MEAS_LO_HZ    20.0
#define NA_FIR_MEAS_HI_HZ 20000.0

class NaConf {

private:

  bool quiet;
  int n_convprocs;
  int jack_sample_rate;   // JACK sample rate, probed before parsing (see NatAmbio)
  /* JACK's period, from the same probe. Only the _ms tags need it, and only to
     turn a duration into blocks; zero means the probe could not report it, and
     the tags that depend on it say so rather than guess. */
  int jack_frame_size;
  /* The document this configuration was parsed from, kept for the lifetime of
     the object rather than freed once it has been read. It is what the remote
     manager's "getxmlconfig" answers with, the live values patched over it:
     built out of the parsed structures instead, a dump would have to reproduce
     a file that is already known to be valid -- and would lose the comments,
     which in a configuration of this kind carry most of the reasoning. */
  xmlDocPtr conf_doc;

  struct coeff* parse_coeff(xmlNodePtr xmlnode);
  struct xtc* parse_xtc(xmlNodePtr xmlnode);
  struct xtc* parse_xtc_asym(xmlNodePtr xmlnode);
  struct lowhigh* parse_lowhigh(xmlNodePtr xmlnode);
  struct fir_filter* parse_fir_filter(xmlNodePtr xmlnode);
  struct loudness* parse_loudness(xmlNodePtr xmlnode);
  struct convol* parse_convol(xmlNodePtr xmlnode);
  struct s_nae* parse_nae(xmlNodePtr xmlnode);
  bool parse_jackinput(xmlNodePtr xmlnode);
  bool parse_jackoutput(xmlNodePtr xmlnode);
  bool parse_remote(xmlNodePtr xmlnode);
  bool parse_setting(xmlNodePtr xmlnode);
  xmlNodePtr findNatambioNode(void);
  xmlNodePtr findNaeNode(const string& nae_name, struct s_nae **parsed);
  bool sndfile_read(struct coeff* coeff);
  struct coeff* find_coeff(string name);
  bool build_convol_coeffs(void);
  bool build_xtc_coeffs(void);
  bool build_lowhigh_coeffs(void);
  bool build_fir_coeffs(void);
  bool build_loudness_coeffs(void);

public:

  vector<struct coeff*> coefslist;
  vector<struct xtc*> xtclist;
  vector<struct lowhigh*> lowhighlist;
  vector<struct fir_filter*> firlist;
  vector<struct loudness*> loudnesslist;
  vector<struct convol*> convollist;
  vector<struct s_nae*> naelist;
  struct jackclient* jackclient;
  /* TCP port of the gain manager (remote.cpp), from <remote>. 0 means the tag
     was absent, which is the only way to say "open no socket": there is no
     default port, on purpose. */
  int remote_port;
  
  NaConf(void);
  ~NaConf(void);

  void setQuiet(void) { quiet = true; };
  bool conf_init(string filename, int jack_sample_rate, int jack_frame_size);
  /* The document as parsed. NULL before conf_init() succeeds. Owned here. */
  xmlDocPtr getConfDoc(void) { return conf_doc; };

  /* A value changed while natambio runs, written back into the configuration:
     into the document, which is what a dump answers with, and into the parsed
     structure beside it, so the two faces of this object never disagree.
     Called from the remote manager's thread, the only one that changes
     anything after startup, and never from the RT callback.

     They record what was asked for; they do not apply it. The engines and the
     ports are driven by ioJack and NAE, which slew to the new value -- writing
     it here as well is what makes the change survive into the next run's
     configuration file instead of living only in the running process.

     False if nothing of that name is configured, in which case nothing is
     written. A gain whose <gain> tag was left out of the file (it defaults to
     0 dB) gets one added: the value is no longer the default. */
  bool setPortGainDb(const string& port_name, double gain_db);
  bool setNaeGainDb(const string& nae_name, enum nae_gain which, double gain_db);
  bool setNaePanScale(const string& nae_name, double pan_scale);

  /* The whole configuration as it now stands, as XML: the document that was
     parsed, carrying every value the commands above have written into it since.
     Empty if there is no document, which cannot happen once natambio is up. */
  string dumpConfig(void);
  int getMaxCoeffsSize(void);

};

#endif
