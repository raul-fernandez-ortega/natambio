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
	xtc_asym.c      xtc_asym.h     \
	loudness.c      loudness.h

# dsp.c uses the double-precision FFTW API (fftw_*) for offline filter design;
# fftw3f stays for the RT path via zita-convolver.
libnatdsp_a_CFLAGS  = -Wall -fPIC -O3
libnatdsp_a_CPPFLAGS = $(FFTW3F_CFLAGS) $(FFTW3_CFLAGS)

# `make check`: the asymmetric XTC design must reproduce the symmetric one when
# both sides carry the same parameters. That cross-check is what allows xtc.c
# (mirrored by third-party ports, and therefore frozen) and xtc_asym.c to stay
# two independent implementations without drifting apart.
check_PROGRAMS = test_xtc_asym
TESTS          = test_xtc_asym

test_xtc_asym_SOURCES  = test_xtc_asym.c
test_xtc_asym_CFLAGS   = -Wall -O2
test_xtc_asym_CPPFLAGS = $(FFTW3F_CFLAGS) $(FFTW3_CFLAGS)
test_xtc_asym_LDADD    = libnatdsp.a $(FFTW3F_LIBS) $(FFTW3_LIBS) -lm
