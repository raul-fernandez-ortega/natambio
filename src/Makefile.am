# -----------------------------------------------------------------------------
# NatAmbio -- src/Makefile.am
# -----------------------------------------------------------------------------

bin_PROGRAMS = natambio

# The shared filter-design C code now lives in ../lib (libnatdsp.a); only the
# JACK application's own C++ units are listed here.
natambio_SOURCES = \
	main.cpp        \
	natambio.cpp    \
	naconf.cpp      \
	iojack.cpp      \
	convchannel.cpp \
	nae.cpp         \
	natambio.hpp    \
	naconf.hpp      \
	iojack.hpp      \
	convchannel.hpp \
	nae.hpp         \
	structs.hpp

natambio_CPPFLAGS = \
	-DVERSION=\"$(PACKAGE_VERSION)\" \
	-I$(top_srcdir)/lib \
	$(JACK_CFLAGS)    \
	$(XML2_CFLAGS)    \
	$(SNDFILE_CFLAGS) \
	$(FFTW3F_CFLAGS)  \
	$(FFTW3_CFLAGS)

natambio_CXXFLAGS = -Wall -fPIC -O3 $(CXX_STD_FLAG) $(PTHREAD_FLAGS)

natambio_LDFLAGS = $(PTHREAD_FLAGS)

natambio_LDADD = \
	$(top_builddir)/lib/libnatdsp.a \
	$(JACK_LIBS)             \
	$(XML2_LIBS)             \
	$(SNDFILE_LIBS)          \
	$(FFTW3F_LIBS)           \
	$(FFTW3_LIBS)            \
	$(ZITA_CONVOLVER_LIBS)

EXTRA_DIST = README.md Makefile.simple

CLEANFILES = *~
