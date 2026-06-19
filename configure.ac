# -----------------------------------------------------------------------------
# NatAmbio -- autoconf script
#
# Bootstrap with:   ./autogen.sh
# Then:             ./configure && make && sudo make install
#
# Required tools to bootstrap: autoconf >= 2.69, automake >= 1.15, pkg-config.
# No autoconf-archive macros are used, to keep the bootstrap footprint small.
# -----------------------------------------------------------------------------

AC_PREREQ([2.69])
AC_INIT([NatAmbio], [1.0.0],
        [natambio.audio@gmail.com], [NatAmbio])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.15 foreign subdir-objects -Wall])
AM_SILENT_RULES([yes])

# ------------------------------ Toolchain -------------------------------------
AC_PROG_CC                 # the shared filter-design code in lib/ is plain C
AC_PROG_CXX
AC_PROG_INSTALL
AM_PROG_AR                 # required to build the lib/ convenience static library
AC_PROG_RANLIB
AC_LANG([C++])

# Require C++11 (uniform-initialisation syntax used in src/naconf.cpp etc.).
# Probe in a portable way without depending on autoconf-archive.
AC_MSG_CHECKING([whether $CXX accepts -std=c++11])
ac_save_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS -std=c++11"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([[#include <vector>]],
                   [[std::vector<int> v{1,2,3}; auto p = &v;]])],
  [AC_MSG_RESULT([yes])
   CXX_STD_FLAG="-std=c++11"],
  [CXXFLAGS="$ac_save_CXXFLAGS"
   AC_MSG_RESULT([no])
   AC_MSG_ERROR([a C++11-capable compiler is required])])
CXXFLAGS="$ac_save_CXXFLAGS"
AC_SUBST([CXX_STD_FLAG])

# Pthreads (real-time worker threads, mutex, semaphore). Use -pthread
# directly; this is correct on every supported toolchain (gcc/clang).
PTHREAD_FLAGS="-pthread"
AC_SUBST([PTHREAD_FLAGS])

# --------------------------- pkg-config dependencies --------------------------
PKG_PROG_PKG_CONFIG([0.25])

PKG_CHECK_MODULES([JACK],    [jack         >= 0.124.0])
PKG_CHECK_MODULES([XML2],    [libxml-2.0   >= 2.9.0])
PKG_CHECK_MODULES([SNDFILE], [sndfile      >= 1.0.25])
PKG_CHECK_MODULES([FFTW3F],  [fftw3f       >= 3.3])

# --------------------------- zita-convolver -----------------------------------
# zita-convolver (Fons Adriaensen) does not ship a pkg-config file.
AC_CHECK_HEADER([zita-convolver.h], [],
  [AC_MSG_ERROR([zita-convolver.h not found.
Install Fons Adriaensen's zita-convolver development package
(e.g. libzita-convolver-dev on Debian/Ubuntu).])])

ac_save_LIBS="$LIBS"
LIBS="-lzita-convolver $FFTW3F_LIBS $LIBS"
AC_MSG_CHECKING([for usable libzita-convolver])
AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([[#include <zita-convolver.h>]],
                   [[Convproc p; (void)p.state();]])],
  [AC_MSG_RESULT([yes])
   ZITA_CONVOLVER_LIBS="-lzita-convolver"],
  [AC_MSG_RESULT([no])
   AC_MSG_ERROR([libzita-convolver not found or not linkable.])])
LIBS="$ac_save_LIBS"
AC_SUBST([ZITA_CONVOLVER_LIBS])

# ------------------------------ librt -----------------------------------------
# clock_gettime / shm_open live in librt on glibc < 2.17.
AC_SEARCH_LIBS([clock_gettime], [rt])

# --------------------------- Optional build modes -----------------------------
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],
                  [enable -DDEBUG -DNRTDEBUG diagnostic output @<:@default=no@:>@])],
  [enable_debug=$enableval], [enable_debug=no])
AS_IF([test "x$enable_debug" = "xyes"],
      [AC_DEFINE([DEBUG],   [1], [Enable diagnostic output])
       AC_DEFINE([NRTDEBUG],[1], [Enable non-real-time diagnostic output])])

AC_ARG_ENABLE([rt-debug],
  [AS_HELP_STRING([--enable-rt-debug],
                  [enable -DDEBUG -DRTDEBUG (verbose, real-time unsafe) @<:@default=no@:>@])],
  [enable_rtdebug=$enableval], [enable_rtdebug=no])
AS_IF([test "x$enable_rtdebug" = "xyes"],
      [AC_DEFINE([DEBUG],   [1], [Enable diagnostic output])
       AC_DEFINE([RTDEBUG], [1], [Enable real-time-thread diagnostic output])])

# ------------------------------ Outputs ---------------------------------------
AC_CONFIG_FILES([
  Makefile
  lib/Makefile
  src/Makefile
  tools/xtc_filters/Makefile
  tools/ladspa_nae_natambio/Makefile
])
AC_OUTPUT

# ------------------------------ Summary ---------------------------------------
AC_MSG_NOTICE([
  ================================================================
  $PACKAGE_NAME $PACKAGE_VERSION
  ----------------------------------------------------------------
  prefix .................. ${prefix}
  C++ compiler ............ ${CXX} ${CXX_STD_FLAG}
  debug (non-RT) .......... ${enable_debug}
  debug (RT) .............. ${enable_rtdebug}
  ----------------------------------------------------------------
  Run 'make' to build, then 'make install' to install.
  ================================================================
])
