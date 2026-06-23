# -----------------------------------------------------------------------------
# NatAmbio -- tools/xtc_filters/Makefile.am
#
# Standalone XTC FIR generator. Only main.c is specific to the tool; the XTC
# pipeline (xtc.c -> dsp.c, binaural_cues.c) is reused from ../../lib via the
# libnatdsp.a convenience library. Installed as natambio-xtc-filters.
# -----------------------------------------------------------------------------

bin_PROGRAMS = natambio-xtc-filters

natambio_xtc_filters_SOURCES = main.c

natambio_xtc_filters_CPPFLAGS = \
	-I$(top_srcdir)/lib \
	$(SNDFILE_CFLAGS)   \
	$(FFTW3F_CFLAGS)

natambio_xtc_filters_CFLAGS = -Wall -O3

natambio_xtc_filters_LDADD = \
	$(top_builddir)/lib/libnatdsp.a \
	$(SNDFILE_LIBS) \
	$(FFTW3F_LIBS)  \
	-lm

EXTRA_DIST = Makefile.simple README.md

CLEANFILES = *~
