# xtc_filters

Standalone command-line generators for **XTC** (crosstalk cancellation) FIR
filters, in two flavours:

| Tool | Layout | Filters written |
|---|---|---|
| `natambio-xtc-filters` | symmetric | direct + cross |
| `natambio-xtc-filters-asym` | asymmetric | direct (shared) + cross left + cross right |

Both write 32-bit float WAV files under `./filters/`, and both reuse the
project's shared filter-design code in `../../lib` (`xtc.c` and `xtc_asym.c` →
`dsp.c`, `binaural_cues.c`) — the very same units `natambio` links — so the DSP
is not duplicated. Only the two mains and the local support units (TOML reader,
configuration schema, WAV writer) are specific to these tools.

## Build

Standalone (no autotools):

```sh
make -f Makefile.simple
# optional: also dump the intermediate ILD_*.wav / MP_ILD_*.wav filters
make -f Makefile.simple DEBUG=1
```

Or as part of the top-level autotools build (`./autogen.sh && ./configure &&
make` from the project root), which installs both tools into `$(bindir)`.

## Configuration files

Parameters are given in a TOML file. Four annotated examples ship here:

| File | What it shows |
|---|---|
| `xtc_sym_default.toml` | symmetric, reproducing the built-in defaults |
| `xtc_sym_wide.toml` | symmetric, tuned for a wider image |
| `xtc_asym_geometry.toml` | asymmetric speaker placement |
| `xtc_asym_room.toml` | symmetric placement in an acoustically asymmetric room |

Installed from the Debian packages, these land in
`/usr/share/doc/natambio-tools/examples/`.

The schema uses the same key names for a side in both tools, so `[xtc]`,
`[left]` and `[right]` are interchangeable blocks:

```toml
sample_rate = 48000
filter_len  = 4096
frac_delay  = true    # optional; exact ITD instead of rounded (see below)
model_delay = 64      # optional; bulk delay the fractional path needs

[xtc]                 # [left] and [right] in the asymmetric tool
itd_us      = 170     # inter-aural time difference, microseconds
ild_db      = 14.0    # inter-aural level difference per step, positive dB
ild_alpha   = 2.0     # scale factor of the empirical ILD spectrum model
azimuth_deg = 20      # half-angle between the speakers

[output]
directory = "filters"
prefix    = "my_room" # optional
```

The reader accepts a strict **subset** of TOML — tables, bare keys, integers,
floats, quoted strings, booleans and comments. Arrays, inline tables, dotted
keys and the rest are refused with the line number rather than misread, and an
unknown key is an error, so a typo like `ild_alfa` fails instead of silently
leaving `ild_alpha` at its default. Because the accepted syntax is a subset,
every file here is valid TOML and is read identically by the Python tools in
[`../python_xtc_filters`](../python_xtc_filters), which use the standard
`tomllib`.

## Run

Symmetric — TOML, flags, or both:

```sh
./xtc_filters -c xtc_sym_default.toml
./xtc_filters -t ITD_us -l ILD_dB -a ILD_alpha -z azimuth_deg -r sample_rate -f filter_len
./xtc_filters -c xtc_sym_default.toml -l 12      # file, with one value changed
# defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096 -M 64
# -F : fractional ITD (see below);  -M N : its bulk model delay
```

The flag interface is unchanged from previous versions. `-c` is read where it
appears in the command line, so flags placed **after** it override the file and
flags placed before it do not.

Asymmetric — TOML only:

```sh
./xtc_filters_asym -c xtc_asym_geometry.toml
```

There is no flag interface for the asymmetric tool on purpose: eight numbers on
a command line, half of them differing from the other half by a single letter,
is exactly the shape of mistake that produces a plausible filter for the wrong
geometry.

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
precision, which is what `make check` in `../../lib` verifies.

`model_delay` (`-M`, default 64) is the bulk delay the fractional path adds to
every filter, so that the two-sided impulse response of a fractional shift is
not clipped at n = 0. It is common to all the filters of a design, and XTC
depends only on the delays *between* them, so it costs latency and nothing else
— 1.3 ms at 48 kHz, ~20 dB of margin over the recursion's own floor.

Measured against the modelled plant, the gain over the rounded design is 42 dB
in the lower midrange rising to 58 dB above 8 kHz; the numbers and the
measurement script are in
[`../python_xtc_filters/compare_frac_delay.py`](../python_xtc_filters/compare_frac_delay.py).
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

Symmetric, with no `output.prefix` (the historical filename contract):

```
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
```

With a prefix configured, in either tool:

```
<prefix>_direct.wav
<prefix>_cross.wav                       # symmetric
<prefix>_cross_left.wav                  # asymmetric
<prefix>_cross_right.wav                 # asymmetric
```

The asymmetric tool writes **three** filters, not four: the two direct filters
of the model are identical, because they depend only on the round-trip operator
*P = G_l ∗ G_r*, which is symmetric under exchange of channels. What differs
between channels is the cross filter.

Mind which side feeds which speaker. `<prefix>_cross_left.wav` feeds the **left**
speaker but is built from the **right** side's parameters, because it cancels the
right speaker's leakage into the left ear and the only direct path reaching that
ear is the left speaker's. Everything in that branch is "right" — the ITD, the
ILD and the spectrum — except the speaker that radiates it.

## The balance, in the asymmetric case

These coefficients are **M⁻¹**: the level trim between channels is deliberately
not baked into them, exactly as with the `<xtc_asym>` block of `natambio`. Apply
it downstream as a gain on the two convolutions feeding the same output,
attenuating the channel that arrives louder and never amplifying the other.

This is not cosmetic. Left unadjusted, the crossed terms do not cancel and the
achievable cancellation is capped at roughly 20·log₁₀|1−b|: a 1 dB error puts
the ceiling near −19 dB, a 3 dB error near −11 dB. The listening procedure and
the full argument are in the balance section of
[`docs/xtc/xtc_no_simetrico_en.md`](../../docs/xtc/xtc_no_simetrico_en.md).

## Related

- The same filters can be generated in-process by `natambio` via `<xtc>` and
  `<xtc_asym>` blocks (see `docs/README.CONFIG`); these tools are for producing
  them offline as WAV files.
- Pure-Python equivalents, reading the same TOML files:
  [`../python_xtc_filters`](../python_xtc_filters).
- The model: [`docs/xtc/xtc_filters_en.md`](../../docs/xtc/xtc_filters_en.md)
  (symmetric) and
  [`docs/xtc/xtc_no_simetrico_en.md`](../../docs/xtc/xtc_no_simetrico_en.md)
  (asymmetric).
