# python_xtc_filters

Pure-Python generators for **XTC** (crosstalk cancellation) FIR filters, in two
flavours:

| Script | Layout | Filters written |
|---|---|---|
| `xtc_filters.py` | symmetric | direct + cross |
| `xtc_filters_asym.py` | asymmetric | direct (shared) + cross left + cross right |

Both write 32-bit float WAV files under `./filters/`.

These are the Python counterparts of the C tools in
[`tools/xtc_filters`](../xtc_filters), which link the project's shared
filter-design code in `../../lib` (`xtc.c` / `xtc_asym.c` → `dsp.c` →
`binaural_cues.c`). The scripts reimplement that same pipeline in NumPy/SciPy so
it can run without building the C toolchain — useful for experimentation,
teaching, and cross-checking the C output.

They are a faithful port of the **C tools**, not of the older
`~/ambio_filters/ambio_filters_scipy.py`. The one substantive difference from
that older script is the minimum-phase step: here (as in `lib/dsp.c`) the
homomorphic cepstrum is computed on an ×8 oversampled grid, which keeps the
magnitude error below ~0.0002 dB. The original script transformed at length `n`,
aliasing the cepstrum tail (~0.2 dB drift, amplified to ~5 % through the 16
chained XTC convolutions). Output filters are therefore equivalent to the C
tools', and use the same output-filename contract.

`xtc_filters_asym.py` imports the DSP primitives (ILD model, minimum phase,
constants) from `xtc_filters.py` rather than copying them. The C side duplicates
them in `xtc_asym.c` instead, but only because `lib/xtc.c` is mirrored by
third-party ports and has to stay byte-stable; no such constraint applies here.

## Platform

The scripts are cross-platform: they depend only on `numpy`, `scipy` and
`soundfile` (all shipping wheels for Windows, macOS and Linux) plus `tomllib`
from the standard library, and use no POSIX-only calls, so they run unchanged on
**GNU/Linux and Microsoft Windows** (and macOS). On Windows, run them directly
with `python xtc_filters.py ...` — the autotools `make install` step and the
`natambio-xtc-filters-py` launchers are Unix-only, but they are just packaging
conveniences, not requirements.

## Requirements

```sh
pip install -r requirements.txt   # numpy, scipy, soundfile
```

TOML parsing uses `tomllib` from the standard library on **Python 3.11 or
later**, so nothing extra is needed there. On older interpreters
`requirements.txt` pulls in `tomli`, which the scripts fall back to
automatically.

## Configuration files

Parameters come from a TOML file, read with `-c`. The format is exactly the one
the C tools use, and the annotated examples live with them rather than being
duplicated here, so the two cannot drift apart:

| File | What it shows |
|---|---|
| [`../xtc_filters/xtc_sym_default.toml`](../xtc_filters/xtc_sym_default.toml) | symmetric, reproducing the built-in defaults |
| [`../xtc_filters/xtc_sym_wide.toml`](../xtc_filters/xtc_sym_wide.toml) | symmetric, tuned for a wider image |
| [`../xtc_filters/xtc_asym_geometry.toml`](../xtc_filters/xtc_asym_geometry.toml) | asymmetric speaker placement |
| [`../xtc_filters/xtc_asym_room.toml`](../xtc_filters/xtc_asym_room.toml) | symmetric placement in an acoustically asymmetric room |

Installed from the Debian packages, these land in
`/usr/share/doc/natambio-drc/examples/`.

```toml
sample_rate = 48000
filter_len  = 4096
frac_delay  = true    # optional; exact ITD instead of rounded (see below)
model_delay = 64      # optional; bulk delay the fractional path needs

[xtc]                 # [left] and [right] in the asymmetric script
itd_us      = 170
ild_db      = 14.0
ild_alpha   = 2.0
azimuth_deg = 20

[output]
directory = "filters"
prefix    = "my_room" # optional
```

As in the C tools, an unknown key is an error rather than a silently ignored
line, so a typo like `ild_alfa` fails instead of leaving `ild_alpha` at its
default.

## Run

Symmetric — TOML, flags, or both:

```sh
python3 xtc_filters.py -c ../xtc_filters/xtc_sym_default.toml
python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha -z azimuth_deg -r sample_rate -f filter_len
python3 xtc_filters.py -c ../xtc_filters/xtc_sym_default.toml -l 12   # file, one value changed
# defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096 -M 64
# -F : fractional ITD (see below);  -M N : its bulk model delay
# -d : also dump intermediate ILD_<az>_deg.wav and MP_ILD_<az>_deg.wav
```

`-c` is read where it appears in the command line, so flags placed **after** it
override the file and flags placed before it do not.

Asymmetric — TOML only, for the same reason as in the C tool: eight numbers on a
command line, half of them differing from the other half by a single letter, is
exactly the shape of mistake that produces a plausible filter for the wrong
geometry.

```sh
python3 xtc_filters_asym.py -c ../xtc_filters/xtc_asym_geometry.toml
```

Installed via the autotools build (`make install`), the two are also available
as the launchers `natambio-xtc-filters-py` and `natambio-xtc-filters-asym-py`.

## Fractional ITD

Off by default. The XTC recursion places its taps at whole samples, so `itd_us`
is rounded first: at 48 kHz 170 µs is 8.16 samples and becomes 8. That rounding
is not free. An ITD error `dt` leaves a residual `2·sin(π·f·dt)` relative to the
cancelling signal, so the design's own crosstalk suppression is capped:

| ITD error | cap at 10 kHz, 48 kHz |
|---|---|
| 0.5 sample | −3.8 dB |
| 0.16 sample (the default 170 µs) | −13.6 dB |
| 0.024 sample | −30 dB |

Set `frac_delay = true` (or pass `-F` to the symmetric tool) and the same
recursion runs in the frequency domain, where a tap is a linear-phase factor
`exp(-j2πfτ)` — the exact band-limited delay operator — and the ITD needs no
rounding. Nothing else in the pipeline changes: the ILD model, the
minimum-phase step and the ladder are the same, rung for rung. With an ITD that
happens to be a whole number of samples the two paths agree to numerical
precision, which is what `make check` in `../../lib` (the C suite) verifies.

`model_delay` (`-M`, default 64) is the bulk delay the fractional path adds to
every filter, so that the two-sided impulse response of a fractional shift is
not clipped at n = 0. It is common to all the filters of a design, and XTC
depends only on the delays *between* them, so it costs latency and nothing else
— 1.3 ms at 48 kHz, ~20 dB of margin over the recursion's own floor.

Measured against the modelled plant, the gain over the rounded design is 42 dB
in the lower midrange rising to 58 dB above 8 kHz; the numbers and the
measurement script are in
[`compare_frac_delay.py`](compare_frac_delay.py).
Those are self-consistency figures — how much of the design's intent survives
the implementation — not a claim about a real room, where HRTF mismatch and head
movement dominate. What changes is that the ITD rounding stops being one of the
limits.

The asymmetric tool has more to gain: it rounds `itd_us` of each side
independently and the round-trip period `itd_left + itd_right` inherits both
errors, so a geometry can carry up to a full sample of period error.

Output filenames gain a `_frac` suffix (symmetric with no prefix), or the
default asymmetric prefix becomes `XTC_asym_frac`, so the two designs cannot
overwrite each other.

## Output

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav   # symmetric, no prefix
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
<prefix>_direct.wav                                              # with a prefix
<prefix>_cross.wav                                               # symmetric
<prefix>_cross_left.wav                                          # asymmetric
<prefix>_cross_right.wav                                         # asymmetric
```

The asymmetric script writes **three** filters, not four: the two direct filters
of the model are identical, because they depend only on the round-trip operator
*P = G_l ∗ G_r*, which is symmetric under exchange of channels.

Mind which side feeds which speaker. `<prefix>_cross_left.wav` feeds the **left**
speaker but is built from the **right** side's parameters, because it cancels the
right speaker's leakage into the left ear and the only direct path reaching that
ear is the left speaker's.

## Pipeline

1. **ILD target curve** (`ild_db_model`): `−ild_log_empirical` above 200 Hz,
   −6 dB/oct extrapolation below, plus a −36 dB/oct HF shelf above 20 kHz.
2. **Linear-phase FIR** via `scipy.signal.firwin2` (Hamming window) on the dense
   grid `1 + 2^ceil(log2(filter_len))`, then RMS-normalised.
3. **Minimum phase** via the ×8 oversampled homomorphic cepstrum, RMS-normalised.
4. **L2 normalisation**, then the **XTC recursion**: 32 alternating direct/cross
   steps in the symmetric case, 16 round-trip rungs in the asymmetric one — the
   same number of terms — each convolved with the min-phase ILD filter and
   truncated to `filter_len`.

In the asymmetric case step 3 runs three times: one filter per side, plus the
round-trip filter, whose log-magnitude is the mean of both sides'. Since the ILD
model is linear in `alpha·sin(theta)`, that mean is obtained by evaluating the
same model at `theta = pi/2` with the mean of the two products — no averaging of
responses. With both sides equal it collapses to the side filter, which is what
makes the asymmetric generator reduce to the symmetric one.

## The balance, in the asymmetric case

These coefficients are **M⁻¹**: the level trim between channels is deliberately
not baked into them. Apply it downstream as a gain on the two convolutions
feeding the same output, attenuating the channel that arrives louder and never
amplifying the other. Left unadjusted, cancellation is capped at roughly
20·log₁₀|1−b| — a 1 dB error puts the ceiling near −19 dB. The listening
procedure is in the balance section of
[`docs/xtc/xtc_no_simetrico_en.md`](../../docs/xtc/xtc_no_simetrico_en.md).

## Related

- The same filters can be generated in-process by `natambio` via `<xtc>` and
  `<xtc_asym>` blocks (see `docs/README.CONFIG`); these scripts produce them
  offline as WAV files.
- The C tools: [`../xtc_filters`](../xtc_filters).
- The model: [`docs/xtc/xtc_filters_en.md`](../../docs/xtc/xtc_filters_en.md)
  (symmetric) and
  [`docs/xtc/xtc_no_simetrico_en.md`](../../docs/xtc/xtc_no_simetrico_en.md)
  (asymmetric).
