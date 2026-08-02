# -----------------------------------------------------------------------------
# NatAmbio -- tools/xtc_filters/Makefile.am
#
# Standalone XTC FIR generators, symmetric and asymmetric. Only the mains and
# the local support units (TOML reader, configuration schema, WAV writer) are
# specific to the tools; the XTC pipelines (xtc.c and xtc_asym.c -> dsp.c,
# binaural_cues.c) are reused from ../../lib via the libnatdsp.a convenience
# library. Installed as natambio-xtc-filters and natambio-xtc-filters-asym.
# -----------------------------------------------------------------------------

bin_PROGRAMS = natambio-xtc-filters natambio-xtc-filters-asym

# Support units shared by both tools. Listed in each program's SOURCES rather
# than built into a separate convenience library: three files do not justify one.
common_sources = toml_min.c toml_min.h xtc_conf.c xtc_conf.h wav_out.c wav_out.h

natambio_xtc_filters_SOURCES      = main.c      $(common_sources)
natambio_xtc_filters_asym_SOURCES = main_asym.c $(common_sources)

common_cppflags = \
	-I$(top_srcdir)/lib \
	$(SNDFILE_CFLAGS)   \
	$(FFTW3F_CFLAGS)    \
	$(FFTW3_CFLAGS)

common_ldadd = \
	$(top_builddir)/lib/libnatdsp.a \
	$(SNDFILE_LIBS) \
	$(FFTW3F_LIBS)  \
	$(FFTW3_LIBS)   \
	-lm

natambio_xtc_filters_CPPFLAGS      = $(common_cppflags)
natambio_xtc_filters_asym_CPPFLAGS = $(common_cppflags)

natambio_xtc_filters_CFLAGS      = -Wall -O3
natambio_xtc_filters_asym_CFLAGS = -Wall -O3

natambio_xtc_filters_LDADD      = $(common_ldadd)
natambio_xtc_filters_asym_LDADD = $(common_ldadd)

EXTRA_DIST = Makefile.simple README.md README_es.md \
	xtc_sym_default.toml xtc_sym_wide.toml \
	xtc_asym_geometry.toml xtc_asym_room.toml

CLEANFILES = *~
