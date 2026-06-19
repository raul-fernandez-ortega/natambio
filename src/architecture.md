# NatAmbio Processor — Architecture

This document describes the internal architecture of the **natambio** processor:
its components, how they are wired together, the initialization sequence, the
real-time processing loop, and the shared data structures. For the user-facing
description of the system and the XML configuration schema, see
[README.md](README.md).

## Implementation

The processor is written in **C++ (C++11)** and built `-O3 -Wall` as a single
JACK client for GNU/Linux. Only six translation units compile into the binary —
`main.cpp`, `natambio.cpp`, `naconf.cpp`, `iojack.cpp`, `convchannel.cpp`, and
`nae.cpp` — plus the shared, plain-C filter-design code in `lib/`
(`dsp.c`, `xtc.c`, `binaural_cues.c`, `loudness.c`), linked in as the
convenience static library `libnatdsp.a`.

It relies on a small set of established Linux-audio libraries:

| Library | Role |
|---|---|
| [JACK](https://jackaudio.org/) (`jack/jack.h`) | Real-time audio I/O graph and process callback |
| [zita-convolver](https://kokkinizita.linuxaudio.org/linuxaudio/) | Partitioned FFT-based convolution engine (`Convproc`) |
| libsndfile | Reading impulse-response audio files |
| libxml2 | Parsing the XML configuration |
| fftw3f | Single-precision FFT used by `dsp.c` to build derived/generated coeffs (also pulled in by zita-convolver) |
| pthreads | Worker threads and POSIX semaphores for the NAE engines |

The design centres on one rule: the JACK process callback runs in the
real-time audio thread and must never allocate memory, lock, block, or perform
I/O. Everything that can be — XML parsing, impulse-response loading, filter
generation, convolver-plan and buffer allocation — is done once, up front,
during initialization. The PCA-based NAE analysis is deliberately offloaded to
dedicated worker threads, signalled by a semaphore and read back through a
mutex, so the callback itself stays light.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         NatAmbio (orchestrator)                 │
│                                                                 │
│  NaConf ──(XML parse)──► coeff / convol / s_nae / jackclient    │
│                                                                 │
│  ioJack ──(JACK graph)──► jack_inputs / jack_outputs            │
│       │                                                         │
│       ├──► ConvChannel[0..N]  ──► Convproc (zita-convolver)     │
│       │        ▲ input from: jack ports, other ConvChannels,    │
│       │        │             NAE outputs                        │
│       │        ▼ output to:  jack ports                         │
│       │                                                         │
│       └──► NAE[0..M]  (each in own real-time thread)            │
│                ▲ semaphore signal from JACK process callback    │
│                ▼ mutex-protected output buffers                 │
└─────────────────────────────────────────────────────────────────┘
```

### Initialization Sequence

```
main()
  ├─ NatAmbio::configXML()     → parse XML, load impulse response files
  ├─ NatAmbio::jackStart()     → create JACK client, register ports
  ├─ NatAmbio::startConvProc() → initialize zita-convolver, create ConvChannels
  ├─ NatAmbio::startNAE() → create NAE instances, spawn RT threads
  └─ NatAmbio::connectPorts()  → connect JACK ports to external destinations
```

### Real-Time Processing Loop (per audio buffer)

```
ioJack::na_process_callback()   [JACK real-time thread]
  ├─ For each JACK input port:
  │    copy buffer → linked ConvChannels and NAE inputs
  ├─ ConvChannel::processInput()   → sum all inputs into convproc input buffer
  ├─ Convproc::process()           → zita-convolver FFT convolution
  ├─ ConvChannel::processOutput()  → apply delay, scale, mix to JACK output ports
  └─ NAE::signal()            → post semaphore to each NAE thread

NAE::thr_process()         [worker real-time thread, per instance]
  ├─ sem_wait()                    → block until signaled by JACK callback
  ├─ PCA decomposition             → see algorithm section below
  └─ write left_out / right_out    → mutex-protected output buffers
```

---

## Components

### `NatAmbio` — Main Orchestrator (`natambio.hpp / natambio.cpp`)

Owns and coordinates all subsystems. Holds the single shared `Convproc` instance and
the collections of `ConvChannel` and `NAE` objects.

Key methods:

| Method | Description |
|---|---|
| `configXML(filename)` | Load and parse the XML configuration |
| `jackStart()` | Create JACK client and register all ports |
| `startConvProc()` | Initialize zita-convolver with impulse responses |
| `startNAE()` | Instantiate NAE engines, start their threads |
| `connectPorts()` | Connect JACK ports to external destinations |
| `newNAE(s_nae*)` | Factory: create and configure a NAE instance |
| `convprocCheckStop()` | Detect convolver error state for main-loop shutdown |

---

### `NaConf` — XML Configuration Parser (`naconf.hpp / naconf.cpp`)

Parses the configuration file using libxml2 and populates:

- `coefslist` — `vector<coeff*>`: impulse response coefficient sets
- `xtclist` — `vector<xtc*>`: XTC filter-generator definitions
- `lowhighlist` — `vector<lowhigh*>`: crossover (low/high) filter-generator definitions
- `loudnesslist` — `vector<loudness*>`: equal-loudness filter-generator definitions
- `convollist` — `vector<convol*>`: convolver routing definitions
- `naelist` — `vector<s_nae*>`: NAE process definitions
- `jackclient` — port name and connection table

All dB `<gain>` fields in the XML (`<coeff>`, `<convol>`,
`<low_and_high_filter>`, and the NAE `*_gain` fields) share one convention —
positive amplifies, negative attenuates — and are converted to a linear factor
via:

```cpp
#define FROM_DB(db) (pow(10, (db) / 20.0))
```

File paths support `~` expansion to the user's home directory.

**Sample rate.** `conf_init()` receives the JACK sample rate, which `NatAmbio`
probes before parsing with a minimal short-lived JACK client
(`queryJackSampleRate()`). It is stored in `NaConf::jack_sample_rate` and used to
(a) generate the xtc / low-high / loudness coeffs (those blocks carry no
`<sample_rate>` tag) and (b) validate every WAV loaded by `sndfile_read()` —
a file whose sample rate differs aborts `conf_init()`. Because all coherence is
checked here, `NatAmbio::startConvProc()` no longer re-checks coeff sample rates.

**Derived coeffs.** A `<coeff>` can be built from the convolution of other
coeffs instead of being loaded from a file (see the `<coeff>` XML section).
While parsing, `parse_coeff()` collects the referenced names into
`coeff::convol_coeffs`; once the whole file has been read, `conf_init()` calls
`build_convol_coeffs()`, which for each derived coeff:

- resolves each `<convol_coeff>` name to an already-built coeff (`find_coeff()`),
- checks that all sources share the same `snfinfo.samplerate` (they always do —
  file coeffs were validated against the JACK rate on load, generated coeffs were
  created at it) and copies it to the derived coeff, whose `snfinfo` is not
  filled from a file,
- convolves them left to right with `fft_convolve_truncate()` from `dsp.c`
  (a single `<convol_coeff>` is simply copied),
- truncates/zero-pads the result to the coeff's `<length>` (or keeps the full
  linear-convolution length when `<length>` is 0) and applies its `<gain>`.

When `<length>` is omitted, `parse_coeff()` already assumes the full
linear-convolution length a priori — the sum of the referenced coeffs' lengths
minus `(count - 1)` — provided every reference is resolvable at parse time;
otherwise (e.g. a reference to a XTC/low-high/loudness filter generated after
parsing) `build_convol_coeffs()` sets it once all coeffs exist. Both arrive at
the same value.

The convolution is done in single precision (`fftwf_*`), consistent with the
rest of the audio path; `dsp.c` keeps `double` signatures and converts at the
boundaries. Sources are resolved in document order, so a derived coeff must be
declared after every coeff it references.

**XTC coeffs.** A `<xtc>` block synthesises a direct/cross XTC
crosstalk-cancellation filter pair from scratch rather than loading or
convolving existing coeffs. `parse_xtc()` fills a `struct xtc` (parameters in
the `<xtc>` XML section); after all `<coeff>` blocks are read, `conf_init()`
calls `build_xtc_coeffs()`, which for each `<xtc>`:

- checks the requested filter names don't collide with an existing coeff,
- runs `process()` from `xtc.c` (the full ILD → minimum-phase → XTC pipeline,
  using `dsp.c` and the log-empirical ILD model in `binaural_cues.c`) to fill two
  `filter_len`-sample `double` buffers,
- wraps each buffer in a freshly allocated `coeff` (via the `make_mem_coeff()`
  helper, which sets `snfinfo.samplerate` to the JACK sample rate) and appends
  both to `coefslist`.

`build_xtc_coeffs()` runs **before** `build_convol_coeffs()`, so a derived
coeff may reference a XTC filter through `<convol_coeff>`.

**Low/high crossover coeffs.** A `<low_and_high_filter>` block synthesises a
complementary low-pass/high-pass FIR pair (e.g. a subwoofer / satellite split)
from scratch. `parse_lowhigh()` fills a `struct lowhigh` (parameters in the
`<low_and_high_filter>` XML section); after all `<coeff>` blocks are read,
`conf_init()` calls `build_lowhigh_coeffs()`, which for each block:

- checks the requested filter names don't collide with an existing coeff,
- designs a linear-phase low-pass with `firwin2()` from `dsp.c` against a local
  dB-magnitude model (flat pass-band at `gain` dB up to `frequency`,
  then a constant `dB_octave` dB/octave roll-off),
- builds the complementary high-pass as `delta − low-pass` (a
  gain-adjusted linear-phase impulse centred at `filter_len/2`), so the
  two sum back to a gain-adjusted delta,
- converts **both** to minimum phase with `minimum_phase()` (those are the
  applied filters), and
- wraps each in a freshly allocated `coeff` via `make_mem_coeff()` (sharing the
  same helper as the XTC path, which sets `snfinfo.samplerate`) and appends
  both to `coefslist`.

Like `build_xtc_coeffs()`, it runs **before** `build_convol_coeffs()`, so a
derived coeff may reference a generated crossover filter through
`<convol_coeff>`.

**Loudness coeffs.** A `<loudness>` block synthesises a single equal-loudness
compensation filter from an isophonic-curve model. `parse_loudness()` fills a
`struct loudness` (parameters in the `<loudness>` XML section); after all
`<coeff>` blocks are read, `conf_init()` calls `build_loudness_coeffs()`, which
for each block:

- checks the requested filter name doesn't collide with an existing coeff,
- calls `loudness_diff_curve()` from `loudness.c` to build the contour
  *difference* (the model's curve at `phon` minus its curve at `ref_phon`, each
  normalised to 0 dB at 1 kHz),
- designs a linear-phase FIR with `firwin2()` (sampling that curve through the
  `loudness_db_model()` callback, which log-interpolates the tabulated points
  and rolls off outside 10 Hz – 20 kHz),
- converts it to minimum phase with `minimum_phase()` (the applied filter), and
- wraps it in a `coeff` via `make_mem_coeff()` and appends it to `coefslist`.

`loudness.c` ports the model tables/formulas from
`~/curvas_isofonicas/generate_isophonic.py` (ISO 226:2003/2023, Fletcher-Munson
1933, A/B/C weighting). Like the others, it runs **before**
`build_convol_coeffs()`, so a derived coeff may convolve the loudness filter
through `<convol_coeff>`.

---

### `ioJack` — JACK I/O (`iojack.hpp / iojack.cpp`)

Manages the JACK client lifecycle and the real-time process callback.

Key responsibilities:
- Register input and output ports (`addInputPort`, `addOutputPort`)
- Wire ports to `ConvChannel` and `NAE` instances
- Drive the audio processing pipeline on every buffer cycle
- Detect and timestamp xrun (buffer underrun) events
- Handle JACK shutdown and latency callbacks

Static JACK callbacks (`jack_process_callback`, `xrun_callback`, etc.) delegate to
member functions via the `void *arg` → `this` pattern.

---

### `ConvChannel` — Convolution Channel (`convchannel.hpp / convchannel.cpp`)

One `ConvChannel` per convolver slot. Wraps a single input/output buffer pair inside
the shared `Convproc` object.

Input sources (mixed together):
- JACK input ports (`jack_inp`)
- Outputs of other `ConvChannel`s (`o_conv_inp`) — enables convolver chaining
- Outputs of `NAE` instances (`o_nae_inp`)

Output sinks:
- One or more JACK output ports (`jack_out`)

Additional features:
- **Delay** (`set_delay`): sample-accurate output delay via a circular buffer
- **Scale** (`set_scale`): linear output gain applied after convolution
- **Bypass mode** (`set_bypass`): routes input directly to output (delta impulse)
  without invoking zita-convolver

---

### `NAE` — Spatial Audio Decomposition (`nae.hpp / nae.cpp`)

Decomposes a stereo input into spatial components using PCA on overlapping windows.

Modes (the XML `<mode>` value maps to the internal mode integer):
- **Mode 0 — `alpha` (front dipole)**: outputs Main signal + Ambience signal
- **Mode 1 — `beta` (rear dipole / surround)**: outputs Surround signal (uses inter-channel correlation)

Gains:
- `gain_main` — main/principal component level
- `gain_amb` — ambience/minor component level
- `gain_surr` — surround level (`beta` mode)

Constants defined in `nae.hpp`:
```cpp
#define ICORRL    10     // inter-channel correlation window length
#define PANCOEFF -2.5    // panorama coefficient
```

#### PCA Algorithm (per buffer)

```
1. Mid/Side decomposition:
     mid  = L + R
     side = L - R

2. Build 2×2 covariance matrix from overlapping steps:
     [ Σ(mid²)    Σ(mid·side) ]
     [ Σ(mid·side) Σ(side²)  ]

3. Closed-form eigenvalue decomposition (eigen_2x2_symmetric):
     solve λ² - tr·λ + det = 0
     compute eigenvectors from (A - λI)

4. Principal eigenvector → main component
   Minor   eigenvector → ambience component

5. `beta` (rear) mode only: compute Pearson inter-channel correlation (icorr)
   to distinguish correlated (center) from decorrelated (surround) content

6. Apply configured gains and write output buffers
```

Helper functions in `nae.cpp`:

| Function | Description |
|---|---|
| `covariance(x, y, N)` | Sample covariance of two signals |
| `correlationPearson(x, y, N)` | Pearson correlation coefficient |
| `correlation(x, y, N)` | Normalized cross-correlation |
| `eigen_2x2_symmetric(a,b,d,...)` | Closed-form 2×2 eigenvalue solver |

---

### `structs.hpp` — Shared Data Structures

| Struct | Fields | Purpose |
|---|---|---|
| `s_nae` | name, mode, gain_main/amb/surr, steps_length, left/right in/out | NAE process config |
| `coeff` | name, filename, channel, skip, length, scale, coeffs, snfinfo, convol_coeffs | Coefficient set: loaded from a file or, when `convol_coeffs` is non-empty, built from the convolution of those named coeffs |
| `xtc` | direct_name, cross_name, itd_us, ild_db, ild_alpha, azimuth_deg, filter_len | XTC filter generator: produces a direct/cross coeff pair via `xtc.c`'s `process()` (at the JACK sample rate) |
| `lowhigh` | low_name, high_name, frequency, db_octave, gain, filter_len | Crossover filter generator: produces a complementary low-pass/high-pass coeff pair via `dsp.c`'s `firwin2()` + `minimum_phase()` (at the JACK sample rate) |
| `loudness` | name, model, phon, ref_phon, filter_len | Equal-loudness filter generator: produces one minimum-phase coeff from an isophonic-curve model (`loudness.c`) via `firwin2()` + `minimum_phase()` (at the JACK sample rate) |
| `convol` | index, name, coeff_name, delay, scale, from_inputs/convols/nae, to_outputs | Convolver routing |
| `jackport` | name, destname | JACK port + external connection destination |
| `jackclient` | name, inports, outports | JACK client with all its ports |

---

## Thread Safety

| Mechanism | Usage |
|---|---|
| `sem_t semaphore` | JACK callback signals each NAE thread once per buffer |
| `pthread_mutex_t mutex` | Protects NAE output buffers during write |
| Real-time scheduling | NAE threads use `SCHED_FIFO` or `SCHED_RR` at the JACK thread priority |

The JACK process callback must not allocate memory, block, or perform I/O. All
buffer management is pre-allocated during initialization.
