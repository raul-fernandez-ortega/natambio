# NatAmbio v1.0

**Nat(ural) Ambio(phonics)**

Real-time audio processing application for spatial audio decomposition and convolution,
built on the JACK Audio Connection Kit.

(c) 2024/2026 Raul Fernandez Ortega — Open source (GPLv3), see LICENSE file.

---

## What this is, and where it fits

*The space is already in the recording. NatAmbio simply makes it audible.*

**NatAmbio** is a spatial playback system for listening to ordinary stereo
recordings, designed to work in professional and — above all — domestic rooms.
A NatAmbio system is made of two inseparable parts:

1. **A specific loudspeaker layout.** NatAmbio follows Robin Miller's
   [PanAmbio](https://www.filmaker.com/papers/SMPTE144-Compatible.pdf) /
   [Ambiophonics](https://en.wikipedia.org/wiki/Ambiophonics) architecture,
   built from stereo *dipoles* (pairs of closely-spaced speakers). A single
   front dipole already delivers most of the spatial effect; a second, rear
   dipole adds ambience, and one or more subwoofers can be added to either
   arrangement. Each dipole uses crosstalk cancellation (XTC) to widen the
   image well beyond the physical speakers.
2. **The NatAmbio DSP processor** — *this software*, which produces all the
   signals that feed those dipoles.

The two are designed to be used together: the same name denotes both the system
and the program at its core.

The key idea is that **all reproduced spatial information comes from the original
stereo signal alone.** NatAmbio does not require special multichannel
recordings and does not synthesise artificial ambience channels: it *extracts*
the ambient information already present in the stereo recording and projects it
into a non-localised sound space around the listener, while keeping a focused,
widened frontal scene. A purely mono recording therefore has no envelopment and
plays back fully centred; live, acoustic, and especially orchestral classical
recordings are where the effect is most evident. The amount of ambience is
configurable, from dry to highly enveloping.

This document describes the processor: a real-time C/C++ JACK client for
GNU/Linux that runs the DSP needed to drive a NatAmbio system.

---

## Overview

The processor performs two complementary tasks on live audio streams, both
driven entirely by a single XML configuration file and running inside the JACK
real-time audio graph:

1. **NAE** (*NatAmbio Ambient Extractor*) — decomposes the incoming stereo
   signal into spatial components (main + ambience for the front dipole, or a
   surround signal for the rear dipole) using Principal Component Analysis
   (PCA). This is what separates the recording's natural ambience from its main
   content and lets the user rebalance the two before the spatial scene is
   built.
2. **Convolution** — applies impulse responses to audio channels via the
   [zita-convolver](https://kokkinizita.linuxaudio.org/linuxaudio/) FFT engine.
   The same engine implements every filtering function of the system: the **XTC**
   crosstalk-cancellation filters that widen each dipole, room **DRC**
   equalisation (e.g. filters built with
   [DRC-FIR](https://drc-fir.sourceforge.net/)), subwoofer crossover handling,
   and configurable **loudness** compensation.

NAE outputs can be routed straight to speakers or chained through convolution
stages, so a single JACK client can run the complete signal chain — from
ambience extraction to the final per-speaker filtering — for a one- or two-dipole
NatAmbio system, with or without subwoofers.

---

## Quick Start

```bash
# Default config file
natambio myconfig.xml

# Suppress banner
natambio -quiet myconfig.xml

# Stop with Ctrl+C (SIGINT)
```

**Usage:**
```
natambio [-quiet] [configuration file]
```

---

## Dependencies

| Library | Purpose |
|---|---|
| JACK (`jack/jack.h`) | Real-time audio I/O graph |
| zita-convolver | Partitioned FFT-based convolution engine |
| libsndfile | Reading impulse response audio files |
| libxml2 | XML configuration parsing |
| fftw3f | Single-precision FFT used by `dsp.c` to build derived coeffs (also pulled in by zita-convolver) |
| pthreads | Threading and POSIX semaphores |

---

## Architecture

The processor's internal design — the component model, the initialization
sequence, the real-time processing loop, the per-class responsibilities, the
shared data structures, and the thread-safety model — is documented separately
in **[architecture.md](architecture.md)**.

---

## XML Configuration Format

### Document Root

Every configuration file is wrapped in a `<main>` root element containing a
single `<natambio>` processing section:

- **`<natambio>`** — the JACK client handling NAE and/or convolution

The parser scans only for `<natambio>`; any other element under `<main>` (for
example a `<crap>` element) is silently ignored, which can be used to disable
entire blocks.

```xml
<main>
  <crap><!-- anything here is ignored --></crap>
  <natambio> ... </natambio>
</main>
```

---

### `<jack_input>` — Input Ports

Declares the JACK client name and all input ports.

```xml
<jack_input>
  <clientname>natambio</clientname>   <!-- JACK client name -->
  <port>
    <name>input_left</name>
    <destname>system:capture_1</destname>   <!-- optional: auto-connect at startup -->
  </port>
  <port>
    <name>input_right</name>
    <destname>system:capture_2</destname>
  </port>
</jack_input>
```

`<destname>` is optional. When omitted the port is created but left unconnected.

---

### `<jack_output>` — Output Ports

Declares all output ports. `<destname>` is again optional.

```xml
<jack_output>
  <port>
    <name>output_left</name>
    <destname>system:playback_1</destname>
  </port>
  <port>
    <name>output_right</name>
    <destname>system:playback_2</destname>
  </port>
  <port>
    <name>surround_output_left</name>
    <destname>system:playback_3</destname>
  </port>
  <port>
    <name>surround_output_right</name>
    <destname>system:playback_4</destname>
  </port>
</jack_output>
```

---

### `<coeff>` — Convolution Coefficient Set

A coeff is obtained either by loading a channel from an audio file, or by
convolving other coeffs together. All `<coeff>` elements must appear before any
`<convol>` that references them, and a derived coeff after every coeff it uses.

**File-loaded coeff:**

```xml
<coeff>
  <name>direct left</name>
  <filename>~/Measurements/current_filters/direct_left.wav</filename>
  <!--skip>5</skip-->       <!-- optional: skip N samples from file start -->
  <length>16384</length>    <!-- samples to load; omit or 0 for entire file -->
</coeff>
```

| Element | Description |
|---|---|
| `<name>` | Unique identifier, referenced by `<convol>/<coeff_name>` |
| `<filename>` | Path to WAV/FLAC/etc. audio file; `~` expands to `$HOME` (required unless `<convol_coeff>` is used) |
| `<channel>` | 1-based channel index to read (optional, default 1) |
| `<skip>` | Samples to skip at the start of the file (optional) |
| `<length>` | Number of samples to load. Omitted (or 0) reads the whole file, setting length to the file's total frame count (minus `<skip>` when skipping) |
| `<gain>` | Per-coefficient gain in dB; positive amplifies, negative attenuates (optional, default 0 dB) |

**Derived coeff** (built from the convolution of other coeffs):

```xml
<coeff>
  <name>XTC EQ direct left</name>
  <convol_coeff>XTC direct</convol_coeff>   <!-- convolved with... -->
  <convol_coeff>DRC EQ left</convol_coeff>   <!-- ...this one -->
  <length>16384</length>                   <!-- result truncated/padded to this length -->
</coeff>
```

| Element | Description |
|---|---|
| `<name>` | Unique identifier, referenced by `<convol>/<coeff_name>` |
| `<convol_coeff>` | Name of another coeff to convolve; repeatable (1..n). One name = copy; several = convolved left to right |
| `<length>` | Output length in samples; truncated/zero-padded to it (0 or omitted keeps the full convolution length) |
| `<gain>` | Extra gain in dB applied to the result; positive amplifies, negative attenuates (optional, default 0 dB) |

When `<convol_coeff>` is present, `<filename>` is ignored. The build happens after
the whole config is parsed, via `NaConf::build_convol_coeffs()` (single-precision
FFT convolution; see the `NaConf` component section). See
`docs/config_samples/convol_drc_xtc.xml` for a full example.

**Special name `delta`**: Using `<coeff_name>delta</coeff_name>` in a `<convol>` activates
bypass mode — input passes through with delay and gain applied but no convolution.

---

### `<xtc>` — XTC Filter Generator Block

Synthesises a direct/cross XTC crosstalk-cancellation filter pair from scratch
and adds them to the coeff list under the given names. Appears inside
`<natambio>`.

```xml
<xtc>
  <itd_us>180</itd_us>
  <ild_db>20.5</ild_db>
  <ild_alpha>1.8</ild_alpha>
  <azimuth_deg>20</azimuth_deg>
  <length>4096</length>
  <direct_filter_name>XTC direct</direct_filter_name>
  <cross_filter_name>XTC cross</cross_filter_name>
</xtc>
```

| Tag | Description |
|---|---|
| `<itd_us>` | Inter-aural time difference (µs); converted to XTC delay using the JACK sample rate (required) |
| `<ild_db>` | Inter-aural level difference per recursion step (dB attenuation) (required) |
| `<ild_alpha>` | Log-empirical ILD model scale factor (required) |
| `<azimuth_deg>` | Source azimuth (degrees) fed to the ILD model (required) |
| `<length>` | Length of each generated filter, samples (required, > 0) |
| `<direct_filter_name>` | Name of the resulting direct-path coeff (required) |
| `<cross_filter_name>` | Name of the resulting cross-path coeff (required) |

**All `<xtc>` parameters are mandatory** — omitting any one is a parse error.

The pair is generated at the JACK sample rate (probed at start-up — there is no
`<sample_rate>` tag) by `NaConf::build_xtc_coeffs()` (see the `NaConf` component
section) before derived coeffs are resolved, so they may be referenced by a
`<convol>`'s `<coeff_name>` or a derived coeff's `<convol_coeff>`. See
`docs/config_samples/convol_drc_xtc.xml` for a full example.

---

### `<low_and_high_filter>` — Crossover Filter Generator Block

Synthesises a complementary low-pass / high-pass FIR pair (e.g. a subwoofer /
satellite crossover) from scratch and adds them to the coeff list under the
given names. Appears inside `<natambio>`.

```xml
<low_and_high_filter>
  <frequency>100.0</frequency>
  <dB_octave>24</dB_octave>
  <gain>0</gain>
  <length>8192</length>
  <low_pass_coeff_name>Low pass FIR</low_pass_coeff_name>
  <high_pass_coeff_name>High pass FIR</high_pass_coeff_name>
</low_and_high_filter>
```

| Tag | Description |
|---|---|
| `<frequency>` | Crossover (cut-off) frequency, Hz (required, > 0) |
| `<dB_octave>` | Low-pass roll-off slope above the crossover, dB/octave (required) |
| `<gain>` | Pass-band gain applied to both filters, dB; positive amplifies, negative attenuates (optional, default 0 dB) |
| `<length>` | Length of each generated filter, samples (required, > 0) |
| `<low_pass_coeff_name>` | Name of the resulting low-pass coeff (required) |
| `<high_pass_coeff_name>` | Name of the resulting high-pass coeff (required) |

**All `<low_and_high_filter>` parameters are mandatory except `<gain>`**
(optional, default 0 dB) — omitting any other is a parse error.

The low-pass is designed with `firwin2()`; the high-pass is its complement
(`delta − low-pass`), so the two sum to an attenuated delta. Both are converted
to minimum phase. The two coeffs are generated at the JACK sample rate (probed
at start-up — there is no `<sample_rate>` tag) by
`NaConf::build_lowhigh_coeffs()` before derived coeffs are resolved, so they may
be referenced by a `<convol>`'s `<coeff_name>` or a derived coeff's
`<convol_coeff>`. See `docs/config_samples/sub_monitors_convol_dipole.xml` for a full example.

---

### `<loudness>` — Equal-Loudness Compensation Filter Block

Synthesises one minimum-phase FIR following an equal-loudness (isophonic)
contour and adds it to the coeff list under the given name. Appears inside
`<natambio>`.

```xml
<loudness>
  <phon>70</phon>
  <ref_phon>90</ref_phon>
  <model>fletcher-munson</model>
  <length>4096</length>
  <filter_name>loudness filter</filter_name>
</loudness>
```

| Tag | Description |
|---|---|
| `<phon>` | Target listening level, phon (required) |
| `<ref_phon>` | Reference (mastering) level, phon; subtracted from the target contour (required) |
| `<model>` | `iso226-2003`, `iso226-2023`, `fletcher-munson`, `a-weighting`, `b-weighting`, `c-weighting` (required) |
| `<length>` | Length of the generated filter, samples (required, > 0) |
| `<filter_name>` | Name of the resulting coeff (required) |

The filter magnitude is the difference between the model's contour at `<phon>`
and at `<ref_phon>` (both normalised to 0 dB at 1 kHz), so it is flat at 1 kHz
and boosts the band extremes when `<phon>` is below `<ref_phon>`. The weighting
models ignore `<phon>` (their difference curve is flat, yielding a unit
impulse). The coeff is generated at the JACK sample rate (probed at start-up —
there is no `<sample_rate>` tag) by `NaConf::build_loudness_coeffs()` before
derived coeffs are resolved, so it may be referenced by a `<convol>`'s
`<coeff_name>` or a derived coeff's `<convol_coeff>`. See
`docs/config_samples/nae_dipole_loudness_drc.xml` for a full example.

---

### `<nae>` — Spatial Decomposition Block

Each `<nae>` block defines **one** PCA decomposition engine (an independent
NAE process running in its own real-time thread). Use several `<nae>`
blocks for several engines. Appears inside `<natambio>`. See
`docs/config_samples/nae_alpha_beta_out.xml` for a full front + rear example.

```xml
<nae>
  <name>front stereo</name>
  <mode>alpha</mode>                              <!-- alpha or beta -->
  <steps_length>5</steps_length>                  <!-- optional; PCA / covariance window in buffer periods; default 5 -->
  <front_gain>0.00</front_gain>                   <!-- alpha mode only -->
  <ambience_gain>4.00</ambience_gain>             <!-- alpha mode only -->
  <input_left>front_input_left</input_left>       <!-- references a <jack_input> port name -->
  <input_right>front_input_right</input_right>
  <front_output_left>front_main_output_left</front_output_left>
  <front_output_right>front_main_output_right</front_output_right>
  <amb_output_left>front_amb_output_left</amb_output_left>
  <amb_output_right>front_amb_output_right</amb_output_right>
</nae>

<nae>
  <name>surround</name>
  <mode>beta</mode>                               <!-- beta -->
  <!-- steps_length omitted -> defaults to 5 -->
  <rear_gain>0.00</rear_gain>                     <!-- beta mode only -->
  <input_left>rear_input_left</input_left>
  <input_right>rear_input_right</input_right>
  <output_left>rear_output_left</output_left>     <!-- jack output port or virtual name -->
  <output_right>rear_output_right</output_right>
</nae>
```

| Element | Description |
|---|---|
| `<name>` | Label for diagnostics (optional) |
| `<mode>` | `alpha` — main + ambience decomposition (front dipole); `beta` — surround extraction (rear dipole) (required) |
| `<steps_length>` | PCA analysis window length, in JACK buffer periods; the covariance spans `steps_length` blocks (optional, default 5) |
| `<front_gain>` | Main-component gain in dB (alpha mode; required) |
| `<ambience_gain>` | Ambience-component gain in dB (alpha mode; required) |
| `<rear_gain>` | Surround-component gain in dB (beta mode; required) |
| `<input_left>` / `<input_right>` | Source `<jack_input>/<port>/<name>` for each channel (required) |
| `<output_left>` / `<output_right>` | Combined output (alpha: main + ambience; beta: surround): a `<jack_output>` port name (direct output) or a virtual name consumed by `<convol>/<from_nae>`. Written in **both** modes |
| `<front_output_left>` / `<front_output_right>` | Optional separate outputs for the main (front) component on its own (alpha mode) |
| `<amb_output_left>` / `<amb_output_right>` | Optional separate outputs for the ambience component on its own (in beta mode these carry the surround signal) |

At least one output per side is required — either `output_left`/`output_right`
or the `front_*`/`amb_*` equivalents. In the example, the front engine is routed
through its separate front/ambience outputs and the rear engine through plain
left/right outputs; each output name is resolved exactly like `<convol>` outputs
(see *Port Name Resolution* below).

**Mode semantics.** `alpha` (front dipole) writes the main component to the
output channels and mixes the ambience in via `<ambience_gain>`; `beta` (rear
dipole) writes a surround signal, using inter-channel correlation to separate
centred from decorrelated content.

**Gain convention.** All dB gains in the config share one convention: the
NAE `*_gain` fields, the `<coeff>`/`<convol>` `<gain>` fields and the
`<low_and_high_filter>` `<gain>` are **direct gains** converted via
`pow(10, dB/20)` — a positive value amplifies, a negative value attenuates, and
0 dB is unity. For the NAE gains, omitting the value for the active mode is a
parse error.

---

### `<convol>` — Convolution Channel

Each `<convol>` defines one filter path through zita-convolver. Inputs from multiple
sources are mixed before convolution.

```xml
<convol>
  <name>direct left</name>
  <coeff_name>direct left</coeff_name>     <!-- references a <coeff>/<name>, or "delta" for bypass -->
  <from_input>input_left</from_input>      <!-- optional: from a <jack_input> port -->
  <from_nae>pn_output_left</from_nae>  <!-- optional: from a <nae> output name -->
  <from_convol>bypass channel</from_convol>      <!-- optional: from another <convol>/<name> -->
  <to_output>output_left</to_output>       <!-- optional: to a <jack_output> port name -->
  <gain>-5.0</gain>                        <!-- output gain in dB; + amplifies, - attenuates -->
  <delay>0</delay>                         <!-- output delay in samples -->
</convol>
```

| Element | Description |
|---|---|
| `<name>` | Unique identifier; used by other `<convol>/<from_convol>` |
| `<coeff_name>` | References a `<coeff>/<name>`, or `delta` for pass-through bypass |
| `<from_input>` | Mix in a JACK input port (can appear multiple times) |
| `<from_nae>` | Mix in a NAE virtual output name (can appear multiple times) |
| `<from_convol>` | Mix in the output of another convolver by its `<name>` (can appear multiple times) |
| `<to_output>` | Send convolver output to a JACK output port (can appear multiple times; omit for intermediate-only convols) |
| `<gain>` | Output gain in dB; positive amplifies, negative attenuates |
| `<delay>` | Output delay in samples |

Multiple `<from_*>` elements of the same or different types are all mixed into the
convolver input. A `<convol>` without `<to_output>` acts as an intermediate stage,
its output available only via `<from_convol>` in other convolvers.

---

### Port Name Resolution

Output names in `<nae>/<output_left>` (and the `front_*`/`amb_*` variants)
and `<convol>/<from_nae>` use a shared virtual namespace:

- If the name matches a `<jack_output>/<port>/<name>` → output goes directly to that JACK port.
- Otherwise → the name is a virtual buffer consumed by `<convol>/<from_nae>`.

This allows NAE outputs to feed either directly to speakers or through further
convolution stages.

---

## Deployment Patterns

The config samples in `docs/config_samples/` demonstrate the main usage patterns:

### 1. NAE only (no convolution)

```
<natambio>
  <jack_input> ... </jack_input>
  <jack_output> ... </jack_output>
  <nae> ... </nae>
</natambio>
```

Example: `nae_only_usb_v01.xml`

---

### 2. Convolution only (no NAE)

```
<natambio>
  <coeff> ... </coeff>
  <jack_input> ... </jack_input>
  <jack_output> ... </jack_output>
  <convol> ... </convol>
</natambio>
```

Example: `convolver_only_usb_v01.xml`

---

### 3. Complete single-process (NAE → Convolution in one JACK client)

NAE outputs use virtual names (`pn_output_left`, `pn_surround_output_left`, …)
consumed by the convol `<from_nae>` elements. Everything runs under one JACK client.

```
<natambio>
  <coeff> ... </coeff>
  <jack_input> ... </jack_input>
  <jack_output> ... </jack_output>
  <nae> ... </nae>
  <convol> ... </convol>       <!-- from_nae references pca outputs -->
</natambio>
```

Examples: `complete_nae_v01.xml`, `complete_nae_usb_v01.xml`

---

### 4. Bypass mix-in (NAE + direct path combined)

An extra pair of JACK inputs bypasses NAE and feeds into the convolver chain via
a `delta` convolver. The delta output is then mixed with the NAE output using
`<from_convol>` alongside `<from_nae>`.

```xml
<!-- Bypass path -->
<convol>
  <name>left nae bypass</name>
  <coeff_name>delta</coeff_name>
  <from_input>input_left_panambio_bypass</from_input>
  <gain>-2.0</gain>
  <delay>0</delay>
</convol>

<!-- Mix NAE + bypass into convolver -->
<convol>
  <name>direct left</name>
  <coeff_name>direct left</coeff_name>
  <from_nae>pn_output_left</from_nae>
  <from_convol>left nae bypass</from_convol>
  <to_output>output_left</to_output>
  <gain>-5.0</gain>
  <delay>0</delay>
</convol>
```

Examples: `complete_nae_v02.xml`, `complete_nae_usb_v02.xml`

---

## File Reference

```
src/                  natambio JACK application (C++)
├── main.cpp          Entry point: argument parsing, init sequence, signal handler, event loop
├── natambio.hpp/.cpp Main orchestrator: owns all subsystems, drives initialization
├── naconf.hpp/.cpp   XML configuration parser (libxml2 + libsndfile)
├── iojack.hpp/.cpp   JACK client, real-time process callback, port management
├── convchannel.hpp/.cpp  Single convolution channel with delay/scale/bypass
├── nae.hpp/.cpp PCA-based stereo spatial decomposition (real-time thread)
└── structs.hpp       Shared data structures and utility macros

lib/                  shared plain-C filter-design code (libnatdsp.a) — used by src/ and tools/
├── dsp.c/.h          Single-precision FFT helpers; fft_convolve_truncate() builds derived coeffs
├── xtc.c/.h         XTC crosstalk-cancellation filter generator; process() backs <xtc> blocks
├── binaural_cues.c/.h  log-empirical ILD model used by xtc.c
└── loudness.c/.h     equal-loudness contour models; back <loudness> blocks

tools/                auxiliary command-line tools that reuse lib/
└── xtc_filters/     standalone XTC FIR generator (main.c + lib/{xtc,dsp,binaural_cues})
```

---

## Error Handling

- XML parsing failures throw `std::runtime_error` with a descriptive message.
- JACK errors are handled via registered callbacks (`error_callback`, `jack_shutdown_callback`).
- `convprocCheckStop()` polls the zita-convolver state in the main loop and exits cleanly if the engine stops.
- All initialization steps return `bool`; `main()` calls `exit(0)` on any failure.
