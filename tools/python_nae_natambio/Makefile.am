# -----------------------------------------------------------------------------
# NatAmbio -- tools/python_nae_natambio/Makefile.am
#
# Pure-Python NAE (Panambio PCA) reference tool. Nothing is compiled here;
# `make install` copies it to $(datadir)/natambio/python_nae_natambio -- the
# SAME layout the Debian package uses (/usr/share/natambio/python_nae_natambio
# under prefix=/usr) -- with a $(bindir)/natambio-nae launcher.
# -----------------------------------------------------------------------------

toolkitdir = $(datadir)/natambio/python_nae_natambio

EXTRA_DIST = nae_natambio.py requirements.txt README.md README_es.md

install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(toolkitdir)"
	$(INSTALL_DATA) $(srcdir)/nae_natambio.py "$(DESTDIR)$(toolkitdir)/nae_natambio.py"

install-exec-local:
	$(MKDIR_P) "$(DESTDIR)$(bindir)"
	@echo "  GEN      $(bindir)/natambio-nae"
	@( echo '#!/bin/sh'; \
	   echo "exec python3 \"$(toolkitdir)/nae_natambio.py\" \"\$$@\"" ) \
	   > "$(DESTDIR)$(bindir)/natambio-nae"
	@chmod 0755 "$(DESTDIR)$(bindir)/natambio-nae"

uninstall-local:
	rm -rf "$(DESTDIR)$(toolkitdir)"
	rm -f "$(DESTDIR)$(bindir)/natambio-nae"

CLEANFILES = *~
