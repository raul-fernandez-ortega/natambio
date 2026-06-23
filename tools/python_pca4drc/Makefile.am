# -----------------------------------------------------------------------------
# NatAmbio -- tools/python_pca4drc/Makefile.am
#
# PCA4DRC measurement toolkit: pure-Python tools plus the measure_pca4drc.sh
# driver and its data (DRC config, measurement XMLs, target/ config/ mic/).
# Nothing is compiled here; `make install` copies the toolkit to
# $(datadir)/natambio/python_pca4drc -- the SAME layout the Debian package uses
# (/usr/share/natambio/python_pca4drc under prefix=/usr) -- and drops thin
# /usr/bin wrappers for the standalone Python tools.
# -----------------------------------------------------------------------------

toolkitdir = $(datadir)/natambio/python_pca4drc

# Standalone Python tools that get a $(bindir) launcher: "command:script".
WRAPPERS = \
	natambio-pca4drc:pca4drc.py \
	natambio-sweepgen:sweepgen.py \
	natambio-fft-convolve:fft_convolve.py \
	natambio-check-capture:check_capture.py \
	natambio-wav2raw:wav2raw.py \
	natambio-raw2wav:raw2wav.py

# Everything shipped in the source tarball (data dirs included recursively).
EXTRA_DIST = \
	pca4drc.py sweepgen.py fft_convolve.py check_capture.py \
	wav2raw.py raw2wav.py \
	measure_pca4drc.sh config.drc requirements.txt README.md \
	half_natambio_measurements_normal.xml \
	half_natambio_measurements_subwoofer.xml \
	full_natambio_measurements_normal.xml \
	full_natambio_measurements_subwoofer.xml \
	target config mic

# Data files: the .py tools (0644), the executable driver (0755), the DRC
# config, the natambio measurement XMLs and the target/ config/ mic/ data dirs.
install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(toolkitdir)"
	$(INSTALL_SCRIPT) $(srcdir)/measure_pca4drc.sh "$(DESTDIR)$(toolkitdir)/measure_pca4drc.sh"
	$(INSTALL_DATA) $(srcdir)/*.py "$(DESTDIR)$(toolkitdir)/"
	$(INSTALL_DATA) $(srcdir)/config.drc "$(DESTDIR)$(toolkitdir)/config.drc"
	$(INSTALL_DATA) $(srcdir)/*_natambio_measurements_*.xml "$(DESTDIR)$(toolkitdir)/"
	cp -a $(srcdir)/target $(srcdir)/config $(srcdir)/mic "$(DESTDIR)$(toolkitdir)/"

# /usr/bin wrappers: thin launchers that exec the installed script. They point
# at the final $(toolkitdir), so they track the configured prefix.
install-exec-local:
	$(MKDIR_P) "$(DESTDIR)$(bindir)"
	@for w in $(WRAPPERS); do \
	  cmd=`echo "$$w" | sed 's/:.*//'`; \
	  scr=`echo "$$w" | sed 's/.*://'`; \
	  echo "  GEN      $(bindir)/$$cmd"; \
	  ( echo '#!/bin/sh'; \
	    echo "exec python3 \"$(toolkitdir)/$$scr\" \"\$$@\"" ) \
	    > "$(DESTDIR)$(bindir)/$$cmd"; \
	  chmod 0755 "$(DESTDIR)$(bindir)/$$cmd"; \
	done

uninstall-local:
	rm -rf "$(DESTDIR)$(toolkitdir)"
	@for w in $(WRAPPERS); do \
	  cmd=`echo "$$w" | sed 's/:.*//'`; \
	  rm -f "$(DESTDIR)$(bindir)/$$cmd"; \
	done

CLEANFILES = *~
