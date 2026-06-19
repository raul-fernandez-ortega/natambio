# NatAmbio

**Nat(ural) Ambio(phonics) Surround System**

NatAmbio is a real-time JACK audio application that combines PCA-based spatial
extraction with a flexible convolution engine. It is driven from a single XML
configuration file and is designed to run as one JACK client.

## What it does

NatAmbio implements two main processing stages:

1. **NAE** — PCA spatial decomposition of a stereo signal into main/ambience
   components (front mode) or surround content (rear mode), using a 2×2 covariance
   eigensolver.
2. **Convolution** — per-channel FFT convolution via `zita-convolver`, with
   arbitrary routing, output gain, delay, and bypass support.

The output of NAE can be routed directly into the convolution graph as virtual
buffers, avoiding extra JACK round-trips.

## Repository layout

- `src/` — JACK application `natambio` (C++).
- `lib/` — shared filter-design code in plain C (`dsp.c`, `xtc.c`,
  `binaural_cues.c`, `loudness.c`), packaged as `libnatdsp.a`.
- `tools/xtc_filters/` — offline XTC FIR generator.
- `tools/ladspa_nae_natambio/` — LADSPA plugin for the PCA-based NAE engine.
- `tools/python_nae_natambio/` — Python offline NAE implementation.
- `tools/python_pca4drc/` — Python utilities for DRC and measurements.

## Dependencies

- JACK (`jack/jack.h`)
- zita-convolver
- libsndfile
- libxml2
- fftw3f
- pthreads

Python tool dependencies:

- Python 3
- `numpy`
- `soundfile`
- `matplotlib`

## Build and install

### Autotools build

```sh
./autogen.sh
./configure
make
sudo make install
```

This builds `natambio` along with selected tools from `tools/`.

### Standalone builds

```sh
cd src && make -f Makefile.simple
cd tools/xtc_filters && make -f Makefile.simple
cd tools/ladspa_nae_natambio && make -f Makefile.simple
```

`tools/xtc_filters/` and `tools/ladspa_nae_natambio/` are included in the
top-level autotools build. The Python utilities are built and used
independently.

## Usage

```sh
natambio [-quiet] <configuration-file.xml>
```

A running JACK server is required before starting `natambio`.

## Configuration

The current XML schema is described in [`README.CONFIG`](README.CONFIG), which
is authoritative for the parser implemented in `src/naconf.cpp`.

Supported top-level blocks in `<natambio>` include:

- `<coeff>` — load impulse responses from audio files, or build derived coeffs
  by convolving other coeffs.
- `<xtc>` — generate a direct/cross XTC filter pair.
- `<low_and_high_filter>` — generate complementary low-pass/high-pass filters.
- `<loudness>` — generate an equal-loudness compensation filter.
- `<convol>` — define one convolution routing channel.
- `<nae>` — define one NAE spatial-decomposition engine.
- `<jack_input>` / `<jack_output>` — define JACK ports and auto-connections.

Example configurations are available in `docs/config_samples/`.

## Auxiliary tools

- `tools/xtc_filters/` — standalone offline XTC FIR generator.
- `tools/ladspa_nae_natambio/` — LADSPA plugin for the NAE PCA engine.
- `tools/python_nae_natambio/` — offline Python NAE implementation.
- `tools/python_pca4drc/` — Python measurement and DRC utilities.

For details on each tool, see the `README.md` inside the corresponding
subdirectory.

## Notes

- The convolution engine is limited to 64 `<convol>` channels by the zita-convolver.
- Filter generation blocks (`<xtc>`, `<low_and_high_filter>`, `<loudness>`) are
  created at the JACK sample rate probed before parsing.
- `<coeff>` blocks can reference generated filters or other coeffs via
  `<convol_coeff>`.

## References

- [`README.CONFIG`](README.CONFIG) — XML schema and parser reference.
- [`src/README.md`](src/README.md) — internal architecture and implementation notes.

## License

Distributed under the GNU General Public License, version 2.
See the [`LICENSE`](LICENSE) file for details.
