# race_filters

Standalone command-line generator for **RACE** (Recursive Ambiophonic Crosstalk
Elimination) FIR filters. It writes the *direct* and *cross* filter pair as
32-bit float WAV files under `./filters/`.

It reuses the project's shared filter-design code in `../../lib`
(`race.c` → `dsp.c`, `binaural_cues.c`) — the very same units `natambio` links —
so the DSP is not duplicated. Only `main.c` (argument parsing + WAV writing) is
specific to this tool.

## Build

Standalone (no autotools):

```sh
make -f Makefile.simple
# optional: also dump the intermediate ILD_*.wav / MP_ILD_*.wav filters
make -f Makefile.simple DEBUG=1
```

Or as part of the top-level autotools build (`./autogen.sh && ./configure &&
make` from the project root) — it is a `noinst` program, built but not installed.

## Run

```sh
./race_filters -t ITD_us -l ILD_dB -a ILD_alpha -z azimuth_deg -r sample_rate -f filter_len
# defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096
```

Output (in `./filters/`):

```
RACE_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
RACE_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
```

The same filters can be generated in-process by `natambio` via a `<race>` block
(see `docs/README.CONFIG`); this tool is for producing them offline as WAV files.
