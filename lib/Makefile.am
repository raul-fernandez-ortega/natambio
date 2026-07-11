# -----------------------------------------------------------------------------
# NatAmbio -- lib/Makefile.am
#
# Shared, plain-C filter-design code, built as a convenience static library that
# both natambio (src/) and the tools (tools/*) link against. Keeping it here
# avoids duplicating the DSP units across builds.
# -----------------------------------------------------------------------------

noinst_LIBRARIES = libnatdsp.a

libnatdsp_a_SOURCES = \
	binaural_cues.c binaural_cues.h \
	dsp.c           dsp.h           \
	xtc.c           xtc.h          \
	loudness.c      loudness.h

# dsp.c uses the double-precision FFTW API (fftw_*) for offline filter design;
# fftw3f stays for the RT path via zita-convolver.
libnatdsp_a_CFLAGS  = -Wall -fPIC -O3
libnatdsp_a_CPPFLAGS = $(FFTW3F_CFLAGS) $(FFTW3_CFLAGS)
