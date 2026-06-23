/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _NA_NATAMBIO_HPP_
#define _NA_NATAMBIO_HPP_

#include "structs.hpp"
#include "naconf.hpp"
#include "convchannel.hpp"
#include "iojack.hpp"
#include "nae.hpp"

#include <vector>
#include <string>
#include <stdexcept>
#include <zita-convolver.h>

class NatAmbio {

private:

  bool quiet;
  unsigned int options;
  Convproc *convproc;
  vector<NAE*> NAEs;
  NaConf *naConf;
  ioJack *naJack;
  vector<ConvChannel*> convChannels;
  int sampleRate;

public:

  NatAmbio(void);
  ~NatAmbio(void);
  void setQuiet(void);
  int queryJackSampleRate(void);
  bool configXML(string fileName);
  bool jackStart(void);
  void connectPorts(void);
  bool startConvProc(void);
  bool startNAE(void);
  NAE *newNAE(struct s_nae *n_nae);
  uint32_t getConvprocState(void) { return convproc->state(); };
  bool convprocCheckStop(void);
  
};

#endif
