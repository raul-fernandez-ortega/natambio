# python_xtc_filters

Pure-Python generator for **XTC** (crosstalk cancellation) FIR filters. It
writes the *direct* and *cross* filter pair as 32-bit float WAV files under
`./filters/`.

This is the Python counterpart of the C tool [`tools/xtc_filters`](../xtc_filters),
which links the project's shared filter-design code in `../../lib`
(`xtc.c` → `dsp.c` → `binaural_cues.c`). This script reimplements that same
pipeline in NumPy/SciPy so it can run without building the C toolchain — useful
for experimentation, teaching, and cross-checking the C output.

It is a faithful port of the **C tool**, not of the older
`~/ambio_filters/ambio_filters_scipy.py`. The one substantive difference from
that older script is the minimum-phase step: here (as in `lib/dsp.c`) the
homomorphic cepstrum is computed on an ×8 oversampled grid, which keeps the
magnitude error below ~0.0002 dB. The original script transformed at length `n`,
aliasing the cepstrum tail (~0.2 dB drift, amplified to ~5 % through the 16
chained XTC convolutions). Output filters are therefore equivalent to the C
tool's, and use the same output-filename contract.

## Platform

The script itself is cross-platform: it depends only on `numpy`, `scipy` and
`soundfile` (all shipping wheels for Windows, macOS and Linux) and uses no
POSIX-only calls, so it runs unchanged on **GNU/Linux and Microsoft Windows**
(and macOS). On Windows, run it directly with `python xtc_filters.py ...` — the
autotools `make install` step and the `natambio-xtc-filters-py` launcher are
Unix-only, but they are just packaging conveniences, not requirements.

## Requirements

```sh
pip install -r requirements.txt   # numpy, scipy, soundfile
```

## Run

```sh
python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha -z azimuth_deg -r sample_rate -f filter_len
# defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096
# -d : also dump intermediate filters/ILD_<az>_deg.wav and MP_ILD_<az>_deg.wav
```

Installed via the autotools build (`make install`), it is also available as the
launcher `natambio-xtc-filters-py`.

Output (in `./filters/`):

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
```

## Pipeline

1. **ILD target curve** (`ild_db_model`): `−ild_log_empirical` above 200 Hz,
   −6 dB/oct extrapolation below, plus a −36 dB/oct HF shelf above 20 kHz.
2. **Linear-phase FIR** via `scipy.signal.firwin2` (Hamming window) on the dense
   grid `1 + 2^ceil(log2(filter_len))`, then RMS-normalised.
3. **Minimum phase** via the ×8 oversampled homomorphic cepstrum, RMS-normalised.
4. **L2 normalisation**, then the **XTC recursion** (`get_xtc`): 32 alternating
   direct/cross steps, each convolved with the min-phase ILD filter and
   truncated to `filter_len`.

The same filters can be generated in-process by `natambio` via an `<xtc>` block
(see `docs/README.CONFIG`); this tool produces them offline as WAV files.
