# NatAmbio — configuration samples

Example XML configurations for `natambio`.  Each file is self-contained and
runnable (adjust JACK port names and DRC WAV paths to your setup before use).

Invented DRC filter filenames (`~/filters/drc_monitor_left.wav`, etc.) are
placeholders — substitute your own room measurements.  Files under
`~/current_filters/` follow the same convention used in these samples.

## NatAmbio as a flexible DSP engine and software patch panel

The way NatAmbio is written makes it much more than a fixed processing chain.
The XML config is a **declarative routing graph**: every named signal — a
JACK port, a NAE virtual output, a convol intermediate result — is just a
string that can appear on either side of any connection.  There are no fixed
topologies baked into the code; the entire signal flow is assembled at init
time by resolving those names against the running JACK graph and the internal
block lists.

This design makes NatAmbio behave **de facto as a software patch panel**:

- Any JACK input port can feed any number of NAE engines and convol blocks
  simultaneously.
- Any NAE output (blended, front-only, or ambience-only) can drive any number
  of downstream convols or go directly to a JACK output port.
- Any convol output can fan out to multiple JACK output ports, or feed
  another convol as a chained stage.
- Multiple `<xtc>`, `<loudness>`, `<low_and_high_filter>` and `<coeff>`
  blocks coexist freely; each just adds a named coefficient to the pool that
  any convol can reference.
- All NAE instances run in parallel real-time threads, signalled
  non-blocking from the JACK callback, so adding more engines costs CPU but
  not latency.

The result is a single real-time JACK client that can simultaneously
decompose, filter, spatialise, equalise, and route audio across an arbitrary
number of output paths — or hand off raw NAE components to other clients on
the JACK graph with no convolution overhead at all.

**Any convolution effect is interchangeable with an external FIR file.**
XTC filters, DRC room corrections, crossover half-bands, loudness curves —
every one of these is just a named FIR loaded into the coefficient pool.
NatAmbio reads coefficient files with **libsndfile** (`sf_open` /
`sf_readf_float`), so any tool that can export an impulse response in a
format supported by libsndfile (WAV, AIFF, FLAC, and
[many others](https://libsndfile.github.io/libsndfile/api.html)) can supply
filters directly via a `<coeff>` block — no `<xtc>`, `<loudness>` or
`<low_and_high_filter>` block required.  This means NatAmbio is compatible
with any external FIR design workflow: room correction suites, Matlab/Octave
scripts, Python (scipy, numpy), REW, or any dedicated convolution filter
generator.

The **one effect that cannot be replaced by a convolution file is NAE**.
The NAE alpha and beta engines are not linear filters: they perform a
real-time PCA decomposition of the stereo covariance matrix on a sliding
window, producing a signal-adaptive spatial separation that changes with the
input.  No static FIR impulse response can capture that behaviour.

These examples cover the most common topologies, but with the help of any AI
assistant the patterns shown here can be combined to build arbitrarily complex
configs — for instance, multiple output groups to run parallel paths with
different processing (separate XTC geometries, different loudness targets,
A/B effect comparisons, or independent room correction zones), all inside a
single JACK client.

---

## 2.0 stereo — convol only (no NAE)

Simplest configs: stereo input → convolution → stereo output.  No spatial
decomposition.

| File | Description |
|------|-------------|
| [`convol_drc.xml`](convol_drc.xml) | Stereo DRC only.  One FIR filter per channel loaded from a WAV file, wired straight through. |
| [`convol_drc_xtc.xml`](convol_drc_xtc.xml) | DRC + XTC.  Each XTC path (direct and cross) is pre-convolved with the DRC of the destination speaker via `<convol_coeff>`, giving four convols total. |
| [`convol_drc_xtc_wav.xml`](convol_drc_xtc_wav.xml) | DRC + XTC from WAV files.  Functionally identical to the above but the XTC filters are loaded from `~/filters/xtc_direct.wav` and `~/filters/xtc_cross.wav` instead of being synthesised by an `<xtc>` block.  Use this when the filters were produced by an external tool and must be applied verbatim. |

---

## NAE only — patch panel, no convolution

NatAmbio can run with NAE engines and no convolution stage at all.  When
there are no `<convol>` blocks, the convproc is never started; the binary
acts purely as a real-time JACK patch panel that decomposes the stereo input
and routes the resulting components to any ports on the graph.

The `<output_left>`/`<output_right>` tags in a `<nae>` block can reference
a JACK output port name directly — no delta convol or any other wrapper is
required.  The output ports are left unconnected (no `<destname>`) so they
can be wired freely in qjackctl or switched via `jack_snapshot`.

Typical uses:
- Route NAE components to independent downstream JACK clients (each with its
  own DRC, XTC or room correction), keeping the decomposition stage separate.
- Inspect or meter the NAE output live without running any filters.
- Use as the first client in a multi-stage chain where each `natambio`
  instance handles one processing layer.

| File | Description |
|------|-------------|
| [`nae_alpha_out.xml`](nae_alpha_out.xml) | NAE alpha (front stereo dipole) only.  Blended output (front + ambience) on two unconnected ports `output_L` / `output_R`. |
| [`nae_alpha_beta_out.xml`](nae_alpha_beta_out.xml) | NAE alpha + NAE beta on the same stereo input.  Four unconnected ports: `alpha_L`/`alpha_R` (front dipole blended) and `beta_L`/`beta_R` (rear surround / decorrelated component). |

---

## 2.0 stereo — with NAE front dipole

The NAE alpha engine decomposes the stereo signal into a main (front) component
and an ambience component before further processing.

| File | Description |
|------|-------------|
| [`nae_dipole_drc.xml`](nae_dipole_drc.xml) | NAE front dipole + per-channel DRC.  No subwoofer, no XTC. |
| [`nae_dipole_loudness_drc.xml`](nae_dipole_loudness_drc.xml) | NAE front dipole + equal-loudness compensation + DRC.  The loudness filter is pre-convolved with each DRC FIR so each output path stays a single convolver. |
| [`nae_dipole_drc_xtc.xml`](nae_dipole_drc_xtc.xml) | NAE front dipole + DRC + XTC.  Four XTC paths pre-convolved with the DRC of the destination speaker. |
| [`nae_dipole_drc_xtc_chained.xml`](nae_dipole_drc_xtc_chained.xml) | Chained-convolver variant of the above.  XTC and DRC are kept as separate `<convol>` stages wired with `<from_convol>` instead of a single pre-convolved `<convol_coeff>`.  Stage 1: four intermediate XTC convols (no `<to_output>`); stage 2: two DRC convols that sum their speaker's XTC feeds via `<from_convol>` and drive the outputs.  Mathematically identical result, but XTC and DRC stay independently swappable and DRC is applied once per speaker (2 filters) rather than baked into all four paths.  **Latency cost:** unlike the single-step version, each chained convol reads its input from the previous JACK period, so the `<from_convol>` hand-off consumes one full jackd cycle (`n_samples`).  This doubles the inherent convolution delay to `2 × n_samples` (and each further chained stage adds another `n_samples`). |
| [`nae_dipole_loudness_drc_xtc.xml`](nae_dipole_loudness_drc_xtc.xml) | NAE front dipole + equal-loudness compensation + DRC + XTC.  Four derived coeffs (XTC × loudness × DRC), one convolver per path. |

---

## 2.1 (stereo + subwoofer) — convol only (no NAE)

A `<low_and_high_filter>` generates the crossover at 120 Hz.  The subwoofer
receives the stereo low-pass sum (−3 dB per branch).  XTC cross paths go to
monitors only — no crossover applied, as cancellation must work at all
frequencies and the monitors' natural roll-off handles the low end.

| File | Description |
|------|-------------|
| [`sub_monitors_convol_dipole.xml`](sub_monitors_convol_dipole.xml) | Crossover only.  HP to monitors, LP to subwoofer.  No DRC, no XTC, no NAE. |
| [`sub_monitors_convol_drc_convol_coeff.xml`](sub_monitors_convol_drc_convol_coeff.xml) | Crossover + DRC via `<convol_coeff>`.  DRC pre-convolved with each crossover half so each output path uses a single convolver. |
| [`sub_monitors_convol_drc_xtc_convol_coeff.xml`](sub_monitors_convol_drc_xtc_convol_coeff.xml) | Crossover + DRC + XTC via `<convol_coeff>`.  Monitor paths: four XTC coeffs each pre-convolved with DRC and HP filter.  Subwoofer path: DRC × LP, no XTC. |
| [`sub_monitors_convol_loudness_drc_xtc_convol_coeff.xml`](sub_monitors_convol_loudness_drc_xtc_convol_coeff.xml) | Crossover + equal-loudness compensation + DRC + XTC via `<convol_coeff>`.  Monitor paths: four coeffs (XTC × loudness × DRC × HP).  Subwoofer path: loudness × DRC × LP, no XTC. |

---

## 2.1 (stereo + subwoofer) — with NAE front dipole

Same crossover logic as the convol-only 2.1 configs above, with the NAE alpha
engine upstream.

| File | Description |
|------|-------------|
| [`sub_monitors_nae_dipole.xml`](sub_monitors_nae_dipole.xml) | NAE front dipole + 120 Hz crossover.  No DRC, no XTC. |
| [`sub_monitors_nae_drc_convol_coeff.xml`](sub_monitors_nae_drc_convol_coeff.xml) | NAE front dipole + crossover + DRC via `<convol_coeff>`. |
| [`sub_monitors_nae_drc_xtc_convol_coeff.xml`](sub_monitors_nae_drc_xtc_convol_coeff.xml) | NAE front dipole + crossover + DRC + XTC via `<convol_coeff>`.  Monitor paths carry XTC; subwoofer path is DRC × LP only. |
| [`sub_monitors_nae_loudness_drc_xtc_convol_coeff.xml`](sub_monitors_nae_loudness_drc_xtc_convol_coeff.xml) | NAE front dipole + crossover + equal-loudness compensation + DRC + XTC via `<convol_coeff>`.  Monitor paths: four coeffs (XTC × loudness × DRC × HP).  Subwoofer path: loudness × DRC × LP, no XTC. |

---

## Split two-client chain

Two complementary configs designed to run as separate JACK clients chained
together.  The separation exposes the four NAE component ports on the JACK
graph so the user can connect or disconnect them individually in qjackctl
without stopping either client.

Start `natambio_xtc_drc` first so its input ports exist when `natambio_nae`
auto-connects on startup.

| File | Client | Description |
|------|--------|-------------|
| [`nae_front_amb_out.xml`](nae_front_amb_out.xml) | `natambio_nae` | NAE alpha only.  Exposes four output ports: `front_L`, `front_R` (main component) and `amb_L`, `amb_R` (ambience component).  `destname` values point to the companion client. |
| [`xtc_drc_front_amb_in.xml`](xtc_drc_front_amb_in.xml) | `natambio_xtc_drc` | XTC + DRC front dipole.  Accepts the four ports above; front and ambience pairs are summed inside each convol before XTC processing.  Disconnecting either pair in qjackctl mutes that component live. |

---

## Multiple parallel output paths

Configs that expose several independent output groups from the same input,
all inside one JACK client.  Output ports are left unconnected so they can
be wired freely in qjackctl or switched between `jack_snapshot` presets.
The pattern scales to any number of paths and arbitrarily complex chains.

| File | Description |
|------|-------------|
| [`multi_out_drc_xtc_bypass.xml`](multi_out_drc_xtc_bypass.xml) | Three parallel paths from a single stereo input: **Path A** (`output_A_L/R`) DRC only; **Path B** (`output_B_L/R`) DRC + XTC; **Path C** (`output_C_L/R`) bypass (`delta`, no processing).  Six output ports left unconnected for free wiring or preset switching. |

---

## A/B switching with jack\_snapshot

Single client exposing two parallel output paths for blind level-matched
A/B tests via `jack_snapshot` preset switching.

| File | Description |
|------|-------------|
| [`nae_xtc_drc_vs_bypass.xml`](nae_xtc_drc_vs_bypass.xml) | **Path A** (`output_L/R`): NAE alpha + XTC + DRC + loudness equal-loudness compensation.  **Path B** (`bypass_L/R`): direct pass-through using the `delta` coeff (no convolution).  All four output ports are left unconnected for jack_snapshot to wire.  The loudness filter and NAE ambience gain together add significant level to path A — expect the bypass to need a positive gain trim before the paths are matched.  Once matched, a random-interval shell script calling `jack_snapshot` can run a blind comparison. |

---

## Full system — two dipoles

Complete front + rear ambiophoic system.  Two NAE engines on the same stereo
input: alpha (front dipole) and beta (rear surround extraction).  Two
independent XTC blocks with separate geometry parameters.  Four DRC filters
(front L/R, rear L/R).  Equal-loudness compensation applied to all paths.

XTC cross paths on the front carry no crossover filter (same rationale as the
2.1 configs above).  Rear speakers are assumed full-range; add a crossover
block if they also feed a subwoofer.

| File | Description |
|------|-------------|
| [`full_dipoles_sub.xml`](full_dipoles_sub.xml) | With subwoofer.  Front direct signal split at 100 Hz: HP to satellite monitors (playback 1/2), LP to sub L/R (playback 3/4).  Rear monitors on playback 5/6. |
| [`full_dipoles_no_sub.xml`](full_dipoles_no_sub.xml) | Without subwoofer.  Front direct paths are full-range (single derived coeff per side).  Rear monitors on playback 3/4. |

---

## Exotic configs

Unusual topologies that push the multi-block, multi-output model beyond
typical use cases.  Intended as starting points for experimentation; tune
gains and geometry parameters to taste.

**No logical limit on block counts.**  The following block types are stored
in plain vectors with no hard cap in the parser or runtime:

| Block | Implementation note |
|-------|---------------------|
| `<nae>` | Each instance gets its own real-time `pthread`.  The JACK callback signals all of them with a non-blocking `sem_post` loop and returns immediately; all NAE threads run their PCA computation in parallel for the next period.  Practical limit is CPU cores, not code. |
| `<loudness>` | Each block appends one minimum-phase FIR to `coefslist` under a unique `filter_name`; the generator iterates the full list at init time.  Multiple blocks with different `<phon>`, `<ref_phon>` or `<model>` values coexist without interference. |
| `<xtc>` | Each block synthesises an independent direct/cross filter pair appended to `coefslist`.  Any number of XTC geometries can coexist in the same config. |
| `<coeff>` / `<convol_coeff>` | Coefficient list is unbounded.  Derived coeffs (`<convol_coeff>`) are resolved in a second pass so forward references work. |
| `<convol>` | The only hard limit is **64 convolution slots** imposed by zita-convolver (`Convproc::MAXPART`).  All other multiplicity is unlimited. |

| File | Description |
|------|-------------|
| [`dual_system_nae_loudness_drc_xtc.xml`](dual_system_nae_loudness_drc_xtc.xml) | Two fully independent front-dipole systems driven from the same stereo input.  Each system has its own NAE alpha engine, XTC block (different geometry), equal-loudness filter (different target phon) and per-channel DRC FIR.  The stereo source is read via two separate pairs of JACK input ports both pointing to the same capture channels.  8 convols total (4 per system: XTC × loudness × DRC left/right, direct and cross).  System 1 → playback 1/2; System 2 → playback 3/4. |
