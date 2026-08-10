/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
}

#include "natambio.hpp"

#define PRESENTATION_STRING \
"\n\
NatAmbio (Nat(ural) Ambio(phonics) Surround) v1.0 (Dec 2025)                       \
(c) Raul Fernandez Ortega\n\
                                                              \
Using zita-convolver library\n				      \
\n"

#define USAGE_STRING \
"Usage: %s [-quiet] [configuration file]\n"

static bool stop  = false;

static void sigint_handler (int)
{
    stop = true;
}


int main(int argc,char *argv[])
{
    char *config_filename = NULL;
    bool quiet = false;
    int n;

    NatAmbio *n_NatAmbio = new NatAmbio();

    for (n = 1; n < argc; n++) {
	if (strcmp(argv[n], "-quiet") == 0) {
	    quiet = true;
	} else {
	    if (config_filename != NULL) {
		break;
	    }
	    config_filename = argv[n];
	}
    }
    if (n != argc) {
	fprintf(stderr, PRESENTATION_STRING);
	fprintf(stderr, USAGE_STRING, argv[0]);
	return 0;
    }
    
    if(!quiet) {
	fprintf(stdout, PRESENTATION_STRING);
    }

    if(quiet) 
      n_NatAmbio->setQuiet();
    /* Every startup failure below exits non-zero: natambio normally runs as a
       systemd service, and exiting 0 on a failed start reports success to the
       service manager -- the run then looks fine in systemctl status even though
       no audio was ever processed. Only the shutdown at the end returns 0. */
    if(!(n_NatAmbio->configXML(config_filename))) {
      delete n_NatAmbio;
      exit(1);
      return 1;
    }
    if(!(n_NatAmbio->jackStart())) {
      delete n_NatAmbio;
      exit(1);
      return 1;
    }
    if(!(n_NatAmbio->startConvProc())) {
      delete n_NatAmbio;
      exit(1);
      return 1;
    }
    if(!(n_NatAmbio->connectPorts())) {
      fprintf(stderr, "natambio: some JACK connections requested by the configuration "
	      "could not be made; refusing to run with an incomplete signal path.\n");
      delete n_NatAmbio;
      exit(1);
      return 1;
    }

    /* start! */
    signal(SIGINT, sigint_handler); 
    while(!stop) {    
      usleep (100000);
      if(n_NatAmbio->convprocCheckStop())
	stop = true;
    }
    delete n_NatAmbio;
    exit(0);
    return 0;
}

