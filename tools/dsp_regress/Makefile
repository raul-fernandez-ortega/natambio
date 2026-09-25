# The probe on its own, for running a single case by hand. regress.sh does not
# use this -- it builds the probe twice, against two different libs, into a
# temporary directory of its own.
SRCDIR = ../../lib
CFLAGS ?= -O3 -Wall -g
all: dsp_regress
dsp_regress: dsp_regress.c $(SRCDIR)/libnatdsp.a
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ dsp_regress.c $(SRCDIR)/libnatdsp.a -lfftw3 -lm
clean:
	rm -f dsp_regress
.PHONY: all clean
