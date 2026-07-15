# Makefile for the NatAmbio XTC sweep LADSPA plugins.
#
#   make            build both .so plugins
#   make install    copy them to ~/.ladspa  (create LADSPA_PATH if needed)
#   make clean      remove the built .so files
#
# Under ecasound, add ~/.ladspa to LADSPA_PATH so it finds them:
#   export LADSPA_PATH=$HOME/.ladspa:/usr/lib/ladspa

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC
LDFLAGS ?= -shared -lm

PLUGINS = xtc_sweep_ladspa.so ild_itd_sweep_ladspa.so
PREFIX  ?= $(HOME)/.ladspa

all: $(PLUGINS)

%.so: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

install: all
	mkdir -p $(PREFIX)
	cp -f $(PLUGINS) $(PREFIX)/

clean:
	rm -f $(PLUGINS)

.PHONY: all install clean
