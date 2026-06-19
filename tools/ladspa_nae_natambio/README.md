# ladspa_nae_natambio

A **LADSPA** plugin port of the `natambio` **NAE** (NatAmbio Ambient Extraction)
engine — the PCA-based spatial decomposition that `natambio` runs in its `<nae>`
blocks. It is an almost-direct translation of that algorithm into a single
LADSPA module, so the same stereo processing can be run outside the JACK
application.

Its purpose is:

- **initial tests** and quick experimentation with the NAE decomposition,
- **algorithm verification** (a `-DDEBUG` build prints a per-block correlation
  table you can inspect or plot), and
- **possible real-time use** with any LADSPA-compatible host (e.g. `ecasound`).

The plugin is self-contained: unlike `tools/xtc_filters`, it does **not** link
the project's shared `lib/` filter-design code.

## What it does

Like the `<nae>` block in `natambio`, it builds a 2×2 covariance matrix of
the mid/side signal over a sliding window of `COVSTEPS` (= 5) buffers,
solves it with a closed-form 2×2 symmetric eigensolver, and projects the signal
onto the resulting principal axes to separate a **main** (front) component from
an **ambience** / surround component. The two NAE modes — **alpha** (α) and
**beta** (β), the same names used in `natambio`'s `<mode>` tag and in the
[docs](../../docs/nae/nae_en.md) — are selected by a control port:

- **alpha** (mode `0`) — front separation. Outputs
  `main·mid_gain + ambience·side_gain` as a **front stereo dipole**, matching
  `natambio`'s `<mode>alpha</mode>`.
- **beta** (mode `1`) — ambience extraction. Outputs only the decorrelated
  ambience/surround component (scaled by the ambience gain), with an
  inter-channel-correlation driven β (beta) coefficient, for an **ambient / rear
  dipole**, matching `natambio`'s `<mode>beta</mode>`.

### Ports

| # | Port | Type | Meaning |
|---|------|------|---------|
| 0 | Left  | audio in  | left input |
| 1 | Right | audio in  | right input |
| 2 | Left  | audio out | left output |
| 3 | Right | audio out | right output |
| 4 | Mode  | control   | `0` = alpha (front separation), `1` = beta (ambience) |
| 5 | Alpha Front gain (dB)    | control | main/front gain, alpha mode |
| 6 | Alpha Ambience Gain (dB) | control | ambience/side gain, alpha mode |
| 7 | Beta Ambience Gain (dB)  | control | surround/ambience gain, beta mode |

Gains are in **dB** and applied as `10^(dB/20)` (positive dB = amplification),
matching the NAE gain convention in `natambio`.

- **Label:** `NAE_NATAMBIO`
- **Unique ID:** `1`

## Build

Standalone (no autotools):

```sh
make -f Makefile.simple                 # -> ladspa_nae_natambio.so
make -f Makefile.simple install         # copies the .so into ~/.ladspa
```

Build with the per-block correlation table enabled (printed to stdout while
processing):

```sh
make -f Makefile.simple DEBUG=1
```

Or as part of the top-level autotools build (`./autogen.sh && ./configure &&
make` from the project root). The module is built by an explicit rule (the
project does not use libtool) and `make install` places it in
`$(libdir)/ladspa`.

## Run

### Offline, over a WAV (LADSPA SDK `applyplugin`)

```sh
# applyplugin <in.wav> <out.wav> <plugin.so> <label> <mode> <alpha_front_dB> <alpha_amb_dB> <beta_amb_dB>
# mode 0 = alpha (front dipole), mode 1 = beta (ambient/rear dipole)
applyplugin in.wav out.wav ./ladspa_nae_natambio.so NAE_NATAMBIO 0 0 0 4
```

The input WAV must be stereo. With a `DEBUG=1` build, a table like the following
is printed (one row per processing block):

```
  Sample    Time(s)      LR_in       Beta       M_LR       S_LR      ML_SL  ...
---------------------------------------------------------------------------- ...
       4     0.1707     0.5098     1.0000     0.9999    -0.9992     0.0288  ...
       5     0.2133     0.4839     1.0000     0.9999    -0.9992     0.1127  ...
```

The warm-up blocks are **not** printed: while the `COVSTEPS`-buffer covariance
window is still filling the correlations would be `nan`, so those rows are
suppressed and the table starts at block index `COVSTEPS-1` (= `4`), as shown
above.

Column legend:

| Column | Meaning |
|--------|---------|
| `Sample`  | processing-block index |
| `Time(s)` | elapsed time (block × buffer / sample rate) |
| `LR_in`   | input left/right correlation |
| `Beta`    | side-channel coefficient built from the L/R (Pearson) correlation (not a correlation itself) |
| `M_LR`    | mid_left / mid_right correlation |
| `S_LR`    | side_left / side_right correlation |
| `ML_SL`   | mid_left / side_left |
| `MR_SR`   | mid_right / side_right |
| `ML_SR`   | mid_left / side_right |
| `MR_SL`   | mid_right / side_left |
| `Main_LR` | main output left/right |
| `Amb_LR`  | ambience output left/right |
| `ML_AL`   | mainL / ambL |
| `MR_AR`   | mainR / ambR |
| `ML_AR`   | mainL / ambR |
| `MR_AL`   | mainR / ambL |

Because the columns are space-aligned and the header row is the only
non-numeric line, the table can be fed directly to plotting tools (e.g.
`gnuplot`) after dropping the header/separator lines.

### Real time (ecasound)

```sh
export LADSPA_PATH=~/.ladspa
# first parameter is the mode: 0 = alpha (front dipole), 1 = beta (ambient/rear dipole)
ecasound -i jack,system:capture_1,system:capture_2 \
         -el:NAE_NATAMBIO,0,0,0,4 \
         -o jack,system:playback_1,system:playback_2
```

> A `DEBUG=1` build prints to stdout from the real-time callback; use the plain
> (non-DEBUG) build for actual real-time playback.

## Relationship to natambio

This plugin and the in-application `<nae>` block implement the same NAE
(NatAmbio Ambient Extraction) PCA decomposition. The plugin is convenient for
offline testing and for hosts that already speak LADSPA; `natambio` runs the
same algorithm in dedicated real-time worker threads inside its JACK client,
chained with the convolution stage.

## License

GPLv3. Author: Raúl Fernández Ortega.
