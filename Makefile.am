# -----------------------------------------------------------------------------
# NatAmbio -- top-level Makefile.am
# -----------------------------------------------------------------------------

ACLOCAL_AMFLAGS = -I m4

# lib first: it builds the shared filter-design convenience library that src
# (and the tools) link against. The python_* dirs compile nothing; they carry
# install rules that copy the Python toolkits to $(datadir)/natambio/... (the
# same layout the Debian package uses) plus their /usr/bin wrappers.
SUBDIRS = lib src tools/xtc_filters tools/ladspa_nae_natambio \
	tools/python_pca4drc tools/python_nae_natambio

dist_doc_DATA = \
	docs/README.md \
	docs/README.CONFIG \
	AUTHORS      \
	LICENSE

EXTRA_DIST = \
	autogen.sh   \
	docs         \
	src/README.md \
	src/Makefile.simple
