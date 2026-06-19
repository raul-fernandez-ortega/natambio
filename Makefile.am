# -----------------------------------------------------------------------------
# NatAmbio -- top-level Makefile.am
# -----------------------------------------------------------------------------

ACLOCAL_AMFLAGS = -I m4

# lib first: it builds the shared filter-design convenience library that src
# (and the tools) link against.
SUBDIRS = lib src tools/xtc_filters tools/ladspa_nae_natambio

dist_doc_DATA = \
	README.md    \
	README.CONFIG \
	AUTHORS      \
	LICENSE

EXTRA_DIST = \
	autogen.sh   \
	docs         \
	src/README.md \
	src/Makefile.simple
