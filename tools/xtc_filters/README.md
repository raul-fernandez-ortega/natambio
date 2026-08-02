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

The schema uses the same key names for a side in both tools, so `[xtc]`,
`[left]` and `[right]` are interchangeable blocks:

```toml
sample_rate = 48000
filter_len  = 4096

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
# defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096
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
