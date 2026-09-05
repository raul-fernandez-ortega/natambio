# nae_bench -- built against the object the main build already produces, so the
# engine under test is byte for byte the one natambio links. Run `make` in
# src/ first.
SRCDIR = ../../src
CXXFLAGS ?= -O2 -g -Wall
all: nae_bench
nae_bench: nae_bench.cpp $(SRCDIR)/natambio-nae.o
	$(CXX) -std=c++11 $(CXXFLAGS) -I$(SRCDIR) -o $@ nae_bench.cpp \
	       $(SRCDIR)/natambio-nae.o $(SRCDIR)/natambio-nae_erb.o -lsndfile -lpthread -lfftw3 -lm
clean:
	rm -f nae_bench
.PHONY: all clean
