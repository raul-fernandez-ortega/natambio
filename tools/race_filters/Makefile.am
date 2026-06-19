# -----------------------------------------------------------------------------
# nat_ambio_surround -- tools/race_filters/Makefile.am
#
# Standalone RACE FIR generator. Only main.c is specific to the tool; the RACE
# pipeline (race.c -> dsp.c, binaural_cues.c) is reused from ../../lib via the
# libnatdsp.a convenience library. Built but not installed (noinst).
# -----------------------------------------------------------------------------

noinst_PROGRAMS = race_filters

race_filters_SOURCES = main.c

race_filters_CPPFLAGS = \
	-I$(top_srcdir)/lib \
	$(SNDFILE_CFLAGS)   \
	$(FFTW3F_CFLAGS)

race_filters_CFLAGS = -Wall -O3

race_filters_LDADD = \
	$(top_builddir)/lib/libnatdsp.a \
	$(SNDFILE_LIBS) \
	$(FFTW3F_LIBS)  \
	-lm

EXTRA_DIST = Makefile.simple README.md

CLEANFILES = *~
