# -----------------------------------------------------------------------------
# NatAmbio -- tools/python_xtc_filters/Makefile.am
#
# Pure-Python XTC filter generators, symmetric and asymmetric (Python
# counterparts of tools/xtc_filters). Nothing is compiled here; `make install`
# copies them to $(datadir)/natambio/python_xtc_filters -- the SAME layout the
# Debian package uses (/usr/share/natambio/python_xtc_filters under prefix=/usr)
# -- with a $(bindir) launcher for each.
# -----------------------------------------------------------------------------

toolkitdir = $(datadir)/natambio/python_xtc_filters

# xtc_conf.py is imported by both scripts, and xtc_filters_asym.py imports the
# DSP primitives from xtc_filters.py, so all three must be installed together.
# compare_frac_delay.py imports xtc_filters.py too and therefore has to sit in
# the same directory; it gets no $(bindir) launcher because it measures a design
# rather than producing one.
python_sources = xtc_filters.py xtc_filters_asym.py xtc_conf.py \
	compare_frac_delay.py

EXTRA_DIST = $(python_sources) requirements.txt README.md README_es.md

install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(toolkitdir)"
	for f in $(python_sources); do \
	  $(INSTALL_DATA) "$(srcdir)/$$f" "$(DESTDIR)$(toolkitdir)/$$f" || exit 1; \
	done

install-exec-local:
	$(MKDIR_P) "$(DESTDIR)$(bindir)"
	@echo "  GEN      $(bindir)/natambio-xtc-filters-py"
	@( echo '#!/bin/sh'; \
	   echo "exec python3 \"$(toolkitdir)/xtc_filters.py\" \"\$$@\"" ) \
	   > "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"
	@chmod 0755 "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"
	@echo "  GEN      $(bindir)/natambio-xtc-filters-asym-py"
	@( echo '#!/bin/sh'; \
	   echo "exec python3 \"$(toolkitdir)/xtc_filters_asym.py\" \"\$$@\"" ) \
	   > "$(DESTDIR)$(bindir)/natambio-xtc-filters-asym-py"
	@chmod 0755 "$(DESTDIR)$(bindir)/natambio-xtc-filters-asym-py"

uninstall-local:
	rm -rf "$(DESTDIR)$(toolkitdir)"
	rm -f "$(DESTDIR)$(bindir)/natambio-xtc-filters-py"
	rm -f "$(DESTDIR)$(bindir)/natambio-xtc-filters-asym-py"

CLEANFILES = *~
