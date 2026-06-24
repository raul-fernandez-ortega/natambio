# -----------------------------------------------------------------------------
# NatAmbio -- tools/ladspa_nae_natambio/Makefile.am
#
# LADSPA plugin port of the natambio NAE (PCA) engine. The plugin is a
# self-contained near-direct translation of src/nae.cpp and does NOT link
# the project's lib/ filter-design code.
#
# The project does not use libtool, so the shared module (.so) is produced by an
# explicit rule rather than a *_LTLIBRARIES target, and installed into the
# conventional LADSPA module directory ($(libdir)/ladspa).
# -----------------------------------------------------------------------------

ladspadir = $(libdir)/ladspa

PLUGIN = ladspa_nae_natambio.so

all-local: $(PLUGIN)

$(PLUGIN): ladspa_nae_natambio.c
	$(CC) $(CFLAGS) $(AM_CFLAGS) -O3 -Wall -fPIC -shared \
		-o $@ $(srcdir)/ladspa_nae_natambio.c -lm

install-data-local: $(PLUGIN)
	$(MKDIR_P) $(DESTDIR)$(ladspadir)
	$(INSTALL_DATA) $(PLUGIN) $(DESTDIR)$(ladspadir)/$(PLUGIN)

uninstall-local:
	rm -f $(DESTDIR)$(ladspadir)/$(PLUGIN)

clean-local:
	rm -f $(PLUGIN)

EXTRA_DIST = ladspa_nae_natambio.c Makefile.simple README.md
