# NatAmbio: Build and Installation Guide

NatAmbio is a spatial stereo playback system that reveals the acoustic ambience
already encoded in a conventional stereo recording. It drives close-spaced stereo
dipoles through XTC crosstalk-cancellation filters — widening the frontal scene
well beyond the loudspeaker positions — and projects the extracted ambient
information into a diffuse, non-localised sound field around the listener. All
processing runs as a real-time JACK audio client on GNU/Linux, configured through
a single XML file.

This guide covers dependencies, building from source, and basic usage.

## What it does

NatAmbio implements two main processing stages:

1. **NAE** — PCA spatial decomposition of a stereo signal into main/ambience
   components (front mode) or surround content (rear mode), using a 2×2 covariance
   eigensolver.
2. **Convolution** — per-channel FFT convolution via `zita-convolver`, with
   arbitrary routing, output gain, delay, and bypass support.

The output of NAE can be routed directly into the convolution graph as virtual
buffers, avoiding extra JACK round-trips.

## Real-time audio requirements

NatAmbio runs as a JACK client with real-time scheduling (`SCHED_FIFO`). Three
conditions must be met before a stable, xrun-free system is achievable.

### PREEMPT_RT kernel

A **PREEMPT_RT** kernel is strongly recommended. On a standard kernel, IRQ threads
are not individually schedulable and cannot be assigned `SCHED_FIFO` priority, which
prevents setting the correct priority hierarchy between the hardware interrupt, the
audio driver, and jackd.

On Debian/Ubuntu the package is `linux-image-rt-amd64`. Quick check:

```sh
uname -v | grep -q PREEMPT_RT && echo "RT kernel OK" || echo "Not an RT kernel"
```

### RT permissions for the audio user

The user that runs jackd must belong to the **`audio`** group and have real-time
resource limits granted. Add to `/etc/security/limits.d/audio.conf`:

```
@audio   -  rtprio     95
@audio   -  memlock    unlimited
```

And to `/etc/security/limits.conf`:

```
@audio soft memlock unlimited
@audio hard memlock unlimited
```

Verify in a session of that user:

```sh
ulimit -r    # → 95
ulimit -l    # → unlimited
```

> **Systemd user services:** PAM limits apply to interactive logins but are not
> inherited by `systemd --user` services. Service units must therefore also set
> `LimitRTPRIO=infinity` and `LimitMEMLOCK=infinity` explicitly.

### IRQ priority for FireWire / FFADO

For FireWire audio interfaces (FFADO backend), the correct priority chain is:

> **FireWire IRQ thread > FFADO threads > jackd > natambio**

If the FireWire IRQ sits below jackd, priority inversion causes xruns. The `rtirq`
service manages this automatically. Install it and configure
`/etc/default/rtirq` with `firewire` at the top of `RTIRQ_NAME_LIST`:

```
RTIRQ_NAME_LIST="firewire snd usb i8042"
RTIRQ_PRIO_HIGH=90
RTIRQ_PRIO_DECR=5
RTIRQ_PRIO_LOW=51
```

With this configuration the FireWire IRQ thread gets priority 90, above the FFADO
internal threads (74–76) and jackd (typically launched with `-P70`). USB interfaces
using the ALSA backend do not require rtirq.

## Repository layout

- `src/` — JACK application `natambio` (C++).
- `lib/` — shared filter-design code in plain C (`dsp.c`, `xtc.c`,
  `binaural_cues.c`, `loudness.c`), packaged as `libnatdsp.a`.
- `tools/xtc_filters/` — offline XTC FIR generator.
- `tools/ladspa_nae_natambio/` — LADSPA plugin for the PCA-based NAE engine.
- `tools/python_nae_natambio/` — Python offline NAE implementation.
- `tools/python_pca4drc/` — Python utilities for DRC and measurements.
- `docs/config_samples/` — ready-to-use example XML configuration files covering common setups.
- `natambio_as_a_service/` — systemd unit files and launcher scripts for running natambio as an unattended user service on a dedicated DSP processor.

## Dependencies

- JACK (`jack/jack.h`)
- zita-convolver
- libsndfile
- libxml2
- fftw3f
- pthreads

Python tool dependencies:

- Python 3
- `numpy` (both tools)
- `soundfile` (both tools)
- `scipy` (python_pca4drc)
- `matplotlib` (python_nae_natambio)

## Build and install

### Download

Clone the repository with git (install it first with `sudo apt install git` on
Debian/Ubuntu if needed):

```sh
git clone https://github.com/raul-fernandez-ortega/natambio.git
cd natambio
```

### Autotools build

```sh
./autogen.sh
./configure                 # add --prefix=/usr to match the Debian layout
make
sudo make install
```

This builds `natambio` along with the tools from `tools/` and installs:

- `natambio` and `natambio-xtc-filters` into `$(bindir)`.
- the LADSPA plugin into `$(libdir)/ladspa/`.
- the Python toolkits into `$(datadir)/natambio/python_pca4drc/` and
  `$(datadir)/natambio/python_nae_natambio/`, plus thin `$(bindir)` launchers
  (`natambio-pca4drc`, `natambio-sweepgen`, `natambio-fft-convolve`,
  `natambio-check-capture`, `natambio-wav2raw`, `natambio-raw2wav`,
  `natambio-nae`).

With the default prefix (`/usr/local`) the toolkits land under
`/usr/local/share/natambio/…`. Configure with `--prefix=/usr` to get the exact
same paths as the Debian package (`/usr/share/natambio/…`, `/usr/bin/…`). The
launchers track the configured prefix, so they always point at the matching
toolkit location. Undo an install with `sudo make uninstall`.

### Standalone builds

```sh
cd src && make -f Makefile.simple
cd tools/xtc_filters && make -f Makefile.simple
cd tools/ladspa_nae_natambio && make -f Makefile.simple
```

`tools/xtc_filters/` and `tools/ladspa_nae_natambio/` are included in the
top-level autotools build. The Python utilities are built and used
independently.

## Debian packages

On Debian/Ubuntu the recommended way to install is via the binary packages built
from `debian/`. The source builds into **four** binary packages, so you only
install what you need:

| Package | Arch | Contents | Key depends |
|---|---|---|---|
| `natambio` | any | The real-time JACK client `/usr/bin/natambio` and example configs in `/usr/share/natambio/examples/`. | `jackd2 \| jackd` (recommended) |
| `natambio-tools` | any | Offline C FIR generators: `/usr/bin/natambio-xtc-filters`. | — |
| `natambio-ladspa` | any | LADSPA plugin `…/ladspa/ladspa_nae_natambio.so` (the NAE/Panambio engine for any LADSPA host). | — |
| `natambio-drc` | all | Python measurement/DRC toolkit + offline NAE: the `natambio-*` Python launchers and their data under `/usr/share/natambio/`. | `python3`, `python3-numpy`, `python3-scipy`, `python3-soundfile` |

`natambio` suggests `natambio-tools` and `natambio-drc`; install the latter if you
are going to measure the room and generate DRC filters.

### Building the packages from source

Install the build dependencies and build the packages:

```sh
# Build dependencies (or: sudo apt build-dep .)
sudo apt install build-essential debhelper dh-make devscripts \
    autoconf automake libtool pkg-config \
    libjack-jackd2-dev libzita-convolver-dev libsndfile1-dev \
    libxml2-dev libfftw3-dev ladspa-sdk

# From the repository root, build the binary packages (no signing):
dpkg-buildpackage -us -uc -b
# (equivalently: debuild -us -uc -b)
```

The resulting `.deb` files are written to the **parent** directory (`../`):
`natambio_<version>_<arch>.deb`, `natambio-tools_…`, `natambio-ladspa_…` and
`natambio-drc_<version>_all.deb`.

### Installing the packages

```sh
cd ..
# Install everything (apt resolves the dependencies automatically):
sudo apt install ./natambio_*.deb ./natambio-tools_*.deb \
    ./natambio-ladspa_*.deb ./natambio-drc_*.deb

# Or only the client + measurement toolkit:
sudo apt install ./natambio_*.deb ./natambio-drc_*.deb
```

> Using `apt install ./<file>.deb` (rather than `dpkg -i`) pulls in the runtime
> dependencies (JACK, fftw, numpy/scipy/soundfile, …) automatically. With
> `sudo dpkg -i *.deb` you must fix any missing dependencies afterwards with
> `sudo apt -f install`.

Remove with `sudo apt remove natambio natambio-tools natambio-ladspa natambio-drc`.

### Installed layout

The packages install under the system prefix `/usr`:

| Path | Provided by |
|---|---|
| `/usr/bin/natambio` | `natambio` |
| `/usr/share/natambio/examples/*.xml` | `natambio` |
| `/usr/bin/natambio-xtc-filters` | `natambio-tools` |
| `/usr/lib/<triplet>/ladspa/ladspa_nae_natambio.so` | `natambio-ladspa` |
| `/usr/bin/natambio-pca4drc`, `-sweepgen`, `-fft-convolve`, `-check-capture`, `-wav2raw`, `-raw2wav`, `-nae` | `natambio-drc` |
| `/usr/share/natambio/python_pca4drc/` (scripts, `config.drc`, measurement XMLs, `target/`, `config/`, `mic/`) | `natambio-drc` |
| `/usr/share/natambio/python_nae_natambio/` | `natambio-drc` |

When using the measurement toolkit installed this way, point `TOOLS_DIR` at
`/usr/share/natambio/python_pca4drc` and run from a writable working directory;
see the measurement guide
[`como_medir_respuestas_impulsivas.md`](como_medir_respuestas_impulsivas.md)
(section "Preparar el directorio de medida").

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

## Typical workflow

1. Optionally, run the offline Python NAE tool (`tools/python_nae_natambio/`) on
   representative WAV files to get a first impression of the spatial extraction
   before committing to a live setup. XTC filters can likewise be generated and
   inspected offline with `tools/xtc_filters/`.

2. Measure the listening room and generate DRC FIR filters using the
   `tools/python_pca4drc/` scripts and [DRC-FIR](https://drc-fir.sourceforge.net/)
   (or start without DRC for initial testing).

3. Choose the sample configuration closest to the intended setup
   from `docs/config_samples/`.

4. Adapt JACK port names and FIR file paths to the local system.

5. Start JACK and verify that all input and output ports appear.

6. Launch natambio with the selected XML configuration.

   > **Before the first run:** check that all output gain values in the XML are
   > well under nominal level. Start low and raise gains gradually across successive
   > restarts — a misconfigured gain can produce a loud burst that damages speakers
   > or hearing.

7. Verify signal routing in qjackctl and adjust gains as required.

8. Fine-tune NAE gains, XTC geometry and loudness compensation
   by listening.

9. Experiment: run blind listening tests, compare configurations, and iterate on
   NAE balance, XTC geometry, loudness curve, and DRC correction until the
   ambient effect and tonal response feel right for the room and the music.

10. Once the desired setup is stable, optionally deploy it as a
    systemd user service using the examples in `natambio_as_a_service/`.

11. Enjoy the music.

## Notes

- The convolution engine is limited to 64 `<convol>` channels by the zita-convolver.
- Filter generation blocks (`<xtc>`, `<low_and_high_filter>`, `<loudness>`) are
  created at the JACK sample rate probed before parsing.
- `<coeff>` blocks can reference generated filters or other coeffs via
  `<convol_coeff>`.

## References

- [`README.CONFIG`](README.CONFIG) — XML schema and parser reference.
- [`src/README.md`](../src/README.md) — internal architecture and implementation notes.

## License

Distributed under the GNU General Public License, version 3.
See the [`LICENSE`](../LICENSE) file for details.
