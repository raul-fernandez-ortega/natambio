# python_pca4drc

*Also available in: [Español](README_es.md)*

PCA of impulse responses for DRC, in Python.

It performs the PCA decomposition of a set of room impulse responses to obtain a
reference impulse from which to generate the FIR correction (DRC) filters; the
foundation of the method is developed in the technical note
[Application of PCA to impulsive acoustic measurements of loudspeakers](../../docs/pca4drc/pca4drc_en.md).
It relies only on `numpy`, `scipy` and `soundfile`, **with no graphical
analysis**, so it runs on both GNU/Linux and MS Windows.

Contents:

- `pca4drc.py` — PCA decomposition of a directory of impulses (see below).
- `sweepgen.py` — generates a log-sweep and its inverse (excitation/
  deconvolution pair): `python sweepgen.py sweep.xml [-s sweep.wav] [-i inverse.wav]` (see below).
- `fft_convolve.py` — FFT convolution of two WAV files (`scipy.signal.fftconvolve`).
  Useful to deconvolve measured sweeps with their inverse to obtain impulses:
  `python fft_convolve.py <wav_1> <wav_2> <out_wav>`.
- `check_capture.py` — analyses a capture and warns about clipping / low level /
  low SNR: `python check_capture.py <wav> [label] [--min-level -40] [--min-snr 20]`.
- `wav2raw.py` — converts WAV to raw (float 32-bit LE, headerless), the format
  DRC reads: `python wav2raw.py <wav> [<wav> ...]` (creates `<name>.raw` for each).
  Exact conversion (ecasound is not used because it clips to [-1, 1]).
- `raw2wav.py` — inverse of `wav2raw.py`: converts raw (float 32-bit LE) to WAV.
  Since raw has no header, the rate is given with `--rate`:
  `python raw2wav.py <raw> [<raw> ...] [--rate 48000]` (creates `<name>.wav`).
- `measure_pca4drc.sh` — template for the complete measurement chain (see below).

## Usage

```sh
python pca4drc.py <impulse_directory> <output_len> [--normalize true|false]
```

- `impulse_directory`: folder with the already-measured impulse responses, in
  WAV format.
- `output_len`: length (in samples) of the generated WAVs.
- `--normalize true|false` (default `true`): if `true`, the components are
  divided by the peak of the principal component (the principal one ends with
  peak 1.0); if `false`, the raw PCA values are kept (unnormalized).

The PCA components are saved in a `pca4drc/` subdirectory **inside** the input
directory (created if missing), numbered by their order in the algorithm starting
at 0: `PCA_0.wav` (principal component), `PCA_1.wav`, ...

## Algorithm

1. Reads all the `.wav` files in the directory with `soundfile`.
2. Locates the peak (absolute maximum) of each impulse.
3. Rewrites each impulse into a signal of length `output_len` centered on its
   peak and applies a Blackman window.
4. Subtracts the mean of each impulse, computes the covariance matrix between
   impulses, its eigenvalues/eigenvectors and projects the impulses onto the
   eigenvectors (PCA).
5. Corrects the polarity, optionally normalizes each component by the maximum of
   the principal component (see `--normalize`) and saves them as WAVs of length
   `output_len`.

## Sweep generation: `sweepgen.py`

Generates the logarithmic excitation sweep and its inverse filter, the pair later
used by the measurement chain (`SWEEP` / `INVERSE`). It is written in pure
Python (numpy + soundfile):

```sh
python sweepgen.py sweep.xml [-s sweep.wav] [-i inverse.wav]
```

The parameters are read from a `<generate_sweep>` XML:

```xml
<generate_sweep>
  <params>
    <sample_rate>48000</sample_rate>
    <amplitude>0.5</amplitude>
    <Hzstart>20</Hzstart>
    <Hzend>20000</Hzend>
    <length>6</length>        <!-- sweep duration, s -->
    <silence>1</silence>      <!-- silence at start and end, s -->
    <leadin>0.05</leadin>     <!-- fraction of the sweep with a fade-in window -->
    <leadout>0.005</leadout>  <!-- fraction of the sweep with a fade-out window -->
  </params>
  <sweep_filename>sweep_48k.wav</sweep_filename>
  <inverse_filename>inverse_48k.wav</inverse_filename>
</generate_sweep>
```

`-s` / `-i` override the file names from the XML. The output is 32-bit
floating-point WAV. The sweep includes the silences; the inverse has exactly the
sweep length (`length`
seconds).

## Complete measurement chain: `measure_pca4drc.sh`

Template derived from `ecasound_script.sh` for the four-loudspeaker panambio
system (JACK + Echo AudioFire 4). It chains five phases:

0. **Sweep** (`sweepgen.py`): generates the excitation log-sweep (`SWEEP`) and its
   inverse (`INVERSE`) from the generation-parameter block (`SWEEP_RATE`,
   `SWEEP_LENGTH`, ...). `SWEEP_RATE` must match the capture sample rate
   (48000 Hz). If you already have a sweep/inverse pair, disable it with
   `DO_SWEEP=0`.
1. **Measurement** (`ecasound`): for each microphone position it plays the sweep
   through each way and records the response. The number of ways depends on
   `FULL_NATAMBIO`: `true` (default) measures the four ways of the full NatAmbio
   (front L/R + rear L/R); `false` measures only 2 loudspeakers (front L/R).
   Before measuring it starts **natambio** with the configuration corresponding
   to `FULL_NATAMBIO` (full/half) and `SUBWOOFER` (subwoofer/normal):
   `{full,half}_natambio_measurements_{subwoofer,normal}.xml`. Its output
   (stdout/stderr) is redirected to `NATAMBIO_LOG` (`/tmp/natambio_measure.log`
   by default) to keep the console clean, and it is checked that it starts and
   registers its JACK ports (otherwise it aborts, showing the end of the log).
   ecasound sends the sweep to natambio's input ports
   (`natambio:front_input_left`, ...). Once running, it prints a **configuration
   report** (full/half mode, subwoofer mode, ways to measure and the actual
   routing of natambio's outputs to the card outputs `system:playback_*`, queried
   from JACK) and asks for **confirmation** before measuring. When the
   measurement finishes, natambio is stopped automatically (SIGINT, escalating to
   SIGTERM/SIGKILL if it does not respond). After each capture, `check_capture.py`
   analyses the WAV and warns about clipping, low level (`MIN_LEVEL`, default
   -40 dBFS) or low SNR (`MIN_SNR`, default 20 dB); if the levels are not valid,
   the measurement does not advance: it asks to readjust the microphone preamp
   gain and repeats it (except under `AUTO=1`).
2. **Impulses** (`fft_convolve.py`): deconvolves each recorded sweep with the
   inverse sweep to obtain the impulse response.
3. **PCA** (`pca4drc.py`): for each way it generates `i_<way>/pca4drc/` with the
   PCA component WAVs and then converts them to `.raw` (float 32-bit LE) with
   `wav2raw.py` in the same directory, to feed DRC.
4. **DRC** (Sbragion's `drc`): for each way it runs `drc` with `config.drc`
   (next to this script), overriding `--BCBaseDir=i_<way>/` (the impulse folder,
   at the same level as the original `p_left/` so the config's relative paths,
   e.g. the target curve `../target/...`, keep resolving) and
   `--BCInFile=pca4drc/PCA_0.raw` (the principal component). When finished, it
   converts the `rps.raw` (`PSOutFile`) and `rms.raw` (`MSOutFile`) outputs,
   generated in `i_<way>/`, to WAV with `raw2wav.py`. Enabled with `DO_DRC`
   (1 by default). If a way fails, it is reported and the rest continue.

### Command-line configuration

All variables have a default value but can be **overridden on the fly** by
prefixing them to the call on the same line (without editing the script):

```sh
VARIABLE=value ./measure_pca4drc.sh
VAR1=a VAR2=b ./measure_pca4drc.sh        # several at once
```

They work this way because the script reads them with
`${VARIABLE:-default_value}`. The most common ones:

```sh
./measure_pca4drc.sh                       # the five phases, interactive (4 loudspeakers, normal)
FULL_NATAMBIO=false ./measure_pca4drc.sh   # 2-loudspeaker system (front L/R only)
SUBWOOFER=true ./measure_pca4drc.sh        # start natambio with the subwoofer config
AUTO=1 ./measure_pca4drc.sh                # no interactive pauses
DO_SWEEP=0 ./measure_pca4drc.sh            # use an existing sweep/inverse
DO_MEASURE=0 ./measure_pca4drc.sh          # skip the measurement (re-process what was measured)
DO_DRC=0 ./measure_pca4drc.sh              # everything except the DRC correction
DO_SWEEP=0 DO_MEASURE=0 DO_IMPULSES=0 ./measure_pca4drc.sh  # PCA + DRC only
DO_MEASURE=0 DO_IMPULSES=0 DO_PCA=0 ./measure_pca4drc.sh    # DRC only over already-generated PCA_0.raw
FULL_NATAMBIO=false NUM_POS=8 ./measure_pca4drc.sh         # 2 loudspeakers, 8 positions
OUTPUT_LEN=65536 PCA_NORMALIZE=false ./measure_pca4drc.sh  # adjust PCA parameters
IN_MEAS=system:capture_2 ./measure_pca4drc.sh             # microphone on another capture input
SELECT_INPUT=1 ./measure_pca4drc.sh                        # choose the microphone input from a menu
```

The five phase switches `DO_SWEEP` / `DO_MEASURE` / `DO_IMPULSES` / `DO_PCA` /
`DO_DRC` are `1` (enabled) or `0` (skipped) and are independent, so they can be
combined to run only the phases of interest.

### JACK output (sweep) and input (microphone) ports

Each measurement is performed by `ecasound` with two chains: one **plays** the
sweep into natambio's input and the other **records** the microphone capture.

- **Output (sweep → natambio):** the destination is set by the `OUT_PORTS` array,
  one entry per way, pointing to natambio's input ports
  (`natambio:front_input_left`, `natambio:front_input_right`, ...). It rarely
  needs changing: natambio fixes those names.
- **Input (microphone → WAV):** a single port common to all ways, the variable
  `IN_MEAS` (default `system:capture_1`, the card's first capture input, where
  the microphone preamp usually is).

ecasound connects to those ports with `jack_auto` (auto-connection): the output
to `OUT_PORTS[$w]` and the input to `IN_MEAS`.

There are two ways to indicate the microphone input:

```sh
IN_MEAS=system:capture_2 ./measure_pca4drc.sh   # set it directly
SELECT_INPUT=1 ./measure_pca4drc.sh             # choose it from an interactive menu
```

With `SELECT_INPUT=1`, after starting natambio the script lists the available
JACK capture ports (`jack_lsp -o`, e.g. `system:capture_*`) and lets you assign
one as `IN_MEAS` by number (0 = keep the current one). It only acts in
interactive mode (ignored with `AUTO=1`) and if `jack_lsp` is available; it also
works in calibration mode (`CALIBRATE=1`).

Full list of environment-configurable variables: `FULL_NATAMBIO` (true = 4
loudspeakers, false = 2) and `SUBWOOFER` (true = config with subwoofer, false =
normal); natambio startup `NATAMBIO_BIN`, `NATAMBIO_CONFIG` (by default the XML
chosen according to `FULL_NATAMBIO`/`SUBWOOFER`), `NATAMBIO_LOG`; sweep generation
(Phase 0) `SWEEP_RATE`, `SWEEP_AMPLITUDE`, `SWEEP_HZSTART`, `SWEEP_HZEND`,
`SWEEP_LENGTH`, `SWEEP_SILENCE`, `SWEEP_LEADIN`, `SWEEP_LEADOUT`; measurement
`NUM_POS`, `SWEEP`, `INVERSE`, `IN_MEAS`, `SELECT_INPUT`, `GAIN_OUT`, `GAIN_IN`,
`REC_SECONDS`, `MIN_LEVEL`, `MIN_SNR`; PCA `OUTPUT_LEN`, `PCA_NORMALIZE`; DRC
`DRC_BIN`, `DRC_CONFIG` (by default `config.drc` next to the script), `DRC_PS_OUT`
(rps.raw), `DRC_MS_OUT` (rms.raw); and the phase switches `DO_SWEEP` /
`DO_MEASURE` / `DO_IMPULSES` / `DO_PCA` / `DO_DRC` / `AUTO`. If the `.py` scripts
are not next to the `.sh`, export `TOOLS_DIR` pointing to them.

## Dependencies

```sh
pip install -r requirements.txt
```
