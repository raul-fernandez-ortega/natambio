# -----------------------------------------------------------------------------
# NatAmbio -- tools/python_xtc_filters/Makefile.am
#
# Pure-Python XTC filter generator (Python counterpart of tools/xtc_filters).
# Nothing is compiled here; `make install` copies it to
# $(datadir)/natambio/python_xtc_filters -- the SAME layout the Debian package
# uses (/usr/share/natambio/python_xtc_filters under prefix=/usr) -- with a
# $(bindir)/natambio-xtc-filters-py launcher.
# -----------------------------------------------------------------------------

toolkitdir = $(datadir)/natambio/python_xtc_filters

EXTRA_DIST = xtc_filters.py requirements.txt README.md README_es.md

install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(toolkitdir)"
	$(INSTALL_DATA) $(srcdir)/xtc_filters.py "$(DESTDIR)$(toolkitdir)/xtc_filters.py"

install-exec-local:
	$(MKDIR_P) "$(DESTDIR)$(bindir)"
	@echo "  GEN      $(bindir)/natambio-xtc-filters-py"
	@( echo '#!/bin/sh'; \
	   echo "exec python3 \"$(toolkitdir)/xtc_filters.py\" \"\$$@\"" ) \
	   > "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"
	@chmod 0755 "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"

uninstall-local:
	rm -rf "$(DESTDIR)$(toolkitdir)"
	rm -f "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"

CLEANFILES = *~
