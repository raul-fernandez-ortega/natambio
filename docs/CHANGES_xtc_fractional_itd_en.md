# XTC: fractional ITD

`pan_scale` branch (2026-08-28). Files touched: `lib/dsp.c`, `lib/dsp.h`,
`lib/xtc.c`, `lib/xtc.h`, `lib/xtc_asym.c`, `lib/xtc_asym.h`,
`lib/test_xtc_asym.c`, `src/naconf.cpp`, `src/naconf.hpp`, `src/structs.hpp`,
`tools/xtc_filters/{main.c,main_asym.c,xtc_conf.c,xtc_conf.h,*.toml,README*.md}`,
`tools/python_xtc_filters/{xtc_filters.py,xtc_filters_asym.py,xtc_conf.py,compare_frac_delay.py,Makefile.am,requirements.txt,README*.md}`,
`src/README.md`, `docs/README.CONFIG`.

> **Later note (1.1.0).** The `<frac_delay>` and `<model_delay>` tags this
> document describes no longer exist in the XML configuration: the fractional
> path is the only one, and the model delay is fixed at 1.33 ms --
> `XTC_DEFAULT_MODEL_DELAY` (64 samples) at `XTC_MODEL_DELAY_REF_RATE`
> (48 kHz), scaled to the rate in use by `xtc_model_delay()`. `get_xtc()` and
> the `frac_delay` argument of `process()` are still in `lib/xtc.c`, and the
> offline tools keep their switch. What follows is kept as a record of the
> change as it was made.

**Notice for the [NatAmbio-VST3](https://github.com/digitalfrost84/NatAmbio-VST3)
port: `lib/xtc.c` is no longer frozen.** Until now `lib/xtc_asym.c` deliberately
duplicated the ILD model so as not to touch it, because the port replicates it
byte for byte (see the header comment of `xtc_asym.h` and `lib/Makefile.am`).
This change lifts that freeze: `xtc.c` gains a function and `process()` gains
two arguments. The two files remain independent implementations of the same
model, which is what `test_xtc_asym.c` cross-checks.

---

## 1. The problem

The XTC recursion places its taps by writing to an array index, so the ITD has
to be rounded first:

    lib/xtc.c:217        int itd_samples = (int)round(itd_exact);
    lib/xtc_asym.c:285   the same, once per side

That rounding is not free. If the cancelling signal arrives with a delay error
`dt`, it arrives with a phase error `2πf·dt` and the relative residual is

    residual = |1 − e^{−jθ}| = 2·sin(π·f·dt)

At 48 kHz, with the defaults (`itd_us = 170` → 8.16 samples → 8, error
0.16 samples = 3.33 µs):

| f | cancellation ceiling |
|---|---|
| 5 kHz | −19.6 dB |
| 10 kHz | −13.6 dB |
| 15 kHz | −10.1 dB |

Raising the sample rate does **not** fix it: at 96 kHz, 170 µs are 16.32 samples
and the error is still the same 3.33 µs. Only the worst case improves
(10.4 → 5.2 µs).

The asymmetric case is worse: `delay_l` and `delay_r` are rounded separately and
the round-trip period `T = delay_l + delay_r` inherits both errors, so a geometry
can drag along up to a whole sample of period error. With the example from the
documentation (180 µs and 140 µs at 48 kHz = 8.64 and 6.72 samples, rounded to 9
and 7) the period comes out 16 instead of 15.36.

## 2. The fix

Evaluate the same recursion in frequency, where a tap is a linear phase factor
`exp(-j2πfτ)` -- the exact band-limited delay operator -- instead of an index.
There is no interpolation kernel and no accuracy/length trade-off.

The obvious alternative, a windowed sinc in time, was discarded for two reasons:
windowed kernels do **not** compose exactly, so the error would grow along the
ladder; and with τ ≈ 8 samples the non-causal tail of the first cross tap does
not fit before n = 0.

**Iteration by iteration, the same ladder.** Same taps, same gains, same Horner
order (insert the tap, then filter the whole accumulator with the ILD). The only
thing that changes is how a tap is placed. The ILD model, the minimum-phase step
and the normalisations are untouched.

One detail that looked like an obstacle and was not: `get_xtc()` truncates the
accumulator to `length` on every iteration and the spectral version does not. It
makes no difference -- everything downstream is causal convolution, so a sample
at n ≥ length can never fold back onto n < length. Truncating on every pass and
truncating once at the end leave the same first `length` taps.

### New in `lib/dsp.c` / `dsp.h`

| Function | What it does |
|---|---|
| `dsp_next_pow2()` | exposes the `next_pow2()` that was already private |
| `dsp_rfft()` / `dsp_irfft()` | half-spectrum transforms; the (re, im) pair travels in two real arrays so that `dsp.h` does not drag in FFTW types |
| `dsp_spectrum_add_delayed()` | `acc += gain · exp(-j2πf·delay)`, with fractional `delay` |
| `dsp_spectrum_mul()` | point-wise complex product |

The last two are one Horner iteration over spectra, and they exist so that the
trigonometric loop and the Nyquist-bin rule live in a single place instead of in
the three recursions.

`DSP_MAX_LEN` moves from `dsp.c` to `dsp.h`: whoever sizes their own transform
needs the same bound.

### New in `lib/xtc.c` / `lib/xtc_asym.c`

    int get_xtc_frac(int length, double attenuation, double delay,
                     const double *ild_filter, int model_delay,
                     double *direct_out, double *cross_out);

    int get_xtc_asym_frac(int length, double att_l, double att_r,
                          double delay_l, double delay_r,
                          const double *ild_mean, const double *ild_l, const double *ild_r,
                          int model_delay,
                          double *direct_out, double *cross_left_out, double *cross_right_out);

`process()` and `process_asym()` gain `int frac_delay, int model_delay` before
the output buffers. **It is a signature change**, not a new function: the call
sites have to be updated (`src/naconf.cpp`, `tools/xtc_filters/main*.c`,
`lib/test_xtc_asym.c`).

### The Nyquist bin

A real spectrum of even length has a real Nyquist bin, which a fractional shift
violates. `dsp_spectrum_add_delayed()` keeps the real part there; it is the
standard resolution, it yields the periodic sinc interpolator, and the error
stays confined to that single bin, where the 20 kHz shelf has in any case
already brought `|A|` down to ~0.02.

### `model_delay`

A fractional shift has a two-sided impulse response, so the first cross tap
carries energy at negative time. `model_delay` shifts every filter forward to
preserve it instead of clipping it. It is common to all the filters of a design
and XTC depends only on the delays *between* them, so it costs latency and
nothing else.

Measured (full pipeline, 170 µs, 48 kHz, mean 4–16 kHz):

| `model_delay` | floor reached | latency |
|---|---|---|
| 0 | −62.5 dB | 0 |
| 32 | −79.3 dB | 0.67 ms |
| **64 (default)** | **−85.7 dB** | **1.33 ms** |
| 256 | −100.6 dB | 5.33 ms |
| 1024 | −108.8 dB | 21.3 ms |

The −108.8 dB asymptote is the filter's own truncation to 4096 taps. 64 leaves
~23 dB of margin over that floor, which is plenty.

## 3. Configuration surface

Off by default everywhere. An existing design still produces exactly the same
coefficients.

| Where | How |
|---|---|
| TOML (all four tools) | `frac_delay = true` / `model_delay = 64`, top-level |
| `natambio-xtc-filters` | `-F` and `-M N` |
| `xtc_filters.py` | `-F` and `-M N` |
| `natambio-xtc-filters-asym`, `xtc_filters_asym.py` | TOML only, like the rest of their parameters |
| Engine XML | `<frac_delay>` and `<model_delay>` in `<xtc>` and `<xtc_asym>` |

The two TOML keys go at the top level, not inside `[xtc]`/`[left]`/`[right]`:
they describe the whole design, not one speaker's path, and that is what keeps
those three blocks interchangeable.

`<frac_delay>` is the only boolean in the XML schema, so it gets its own parser
(`parse_bool_tag()` in `naconf.cpp`) instead of a bare `strtol`: writing
`<frac_delay>true</frac_delay>` and having it read as 0 is exactly the silent
misunderstanding this file avoids everywhere else. It accepts `1/0`,
`true/false`, `yes/no`, `on/off`; anything else is a parse error.

### Start-up report

The configuration dump in `naconf.cpp` now shows what each ITD becomes in
samples, which is precisely what the parameter changes:

    	ITD: 180 us (8.640 samples at 48000 Hz, rounded to 9)
    	Fractional ITD: no

    	ITD: 180 us (8.640 samples at 48000 Hz, used exactly)
    	Fractional ITD: yes, model delay 64 samples

`<xtc_asym>` also gains the round-trip period line, which is where the two
roundings compose:

    	Round-trip period: 15.360 samples, rounded to 16   (9 + 7, not 15)

And a `<model_delay>` set without `<frac_delay>` is no longer ignored in
silence: a warning goes to stderr, outside the `!quiet` block, because `-quiet`
silences the configuration dump, not a warning that part of the configuration
does nothing.

`parse_xtc_side()` used to discard in silence any tag that was not one of a
side's four, so `<frac_delay>` written inside `<left>` or `<right>` -- the wrong
place, but the one you try first -- did nothing and said nothing either. It is
now a parse error, and for those two tags in particular the message says where
they belong.

The output names gain `_frac` (the symmetric case, which has no prefix), or the
default asymmetric prefix becomes `XTC_asym_frac`, so that the two designs
cannot tread on each other.

## 4. Verification

All of this is in `make check` (`lib/test_xtc_asym.c`) except where noted.

| Check | Result |
|---|---|
| Whole C path (sym + asym) vs HEAD, WAV to WAV | **0.000e+00 -- bit-identical** |
| Whole Python path vs HEAD, 4 configurations | **0.000e+00 -- bit-identical** |
| `get_xtc_frac` vs `get_xtc` with an integer ITD (250 µs = 12.000 samples) | −309 dB relative |
| The same, asymmetric | −308 dB relative |
| `process_asym` frac vs `process` frac, both sides equal | 2.1e-9 relative (the existing tolerance is 1e-7) |
| `cross_left` vs `cross_right` with equal sides | exactly 0.0 |
| Frac vs integer at 180 µs (8.64 samples): it **must** differ | 0.35 relative |
| C vs Python on the fractional path | ~1e-6, the same FFTW-vs-numpy noise the integer path already had |
| Integer-ITD curve vs the closed form `a·2sin(πf·dt)`, 2–16 kHz | maximum deviation **0.35 dB** |
| Engine XML route: `<xtc>` and `<xtc_asym>`, with and without `<frac_delay>`, invalid boolean, orphan `<model_delay>` | exercised by calling `NaConf::conf_init()` directly, with no JACK server |

The comparison against the closed form is the one that validates the theory, and
it lives in `tools/python_xtc_filters/compare_frac_delay.py`, which measures both
designs against the plant the recursion itself models,
`C(f) = a·A(f)·exp(-j2πf·itd_exact)`.

### Measured gain (full pipeline, 170 µs, 48 kHz)

| band | no filter | integer ITD | fractional ITD | gain |
|---|---|---|---|---|
| 0.25–1 kHz | −8.7 dB | −46.2 | −88.6 | 42.4 dB |
| 1–2 kHz | −10.0 dB | −40.6 | −88.0 | 47.4 dB |
| 2–4 kHz | −11.4 dB | −35.3 | −87.3 | 52.0 dB |
| 4–8 kHz | −13.0 dB | −31.2 | −87.5 | 56.3 dB |
| 8–16 kHz | −14.9 dB | −26.9 | −84.5 | 57.6 dB |

A sweep from 100 to 260 µs gives 57–61 dB consistently, **except at 250 µs**,
where the ITD is exactly 12.000 samples, there is no rounding to correct and the
two paths coincide (−108.6 vs −108.7 dB). That point is the negative control:
the gain only shows up when there is something to gain.

**What these numbers do NOT say.** They are self-consistency figures: they
measure the filter against the plant it was designed for, that is, how much of
the cancellation the design intends survives the implementation. They are not a
claim about a real room, where HRTF mismatch and head movement limit XTC to
−15…−25 dB. What changes is that ITD rounding, which at 8–16 kHz sat at −27 dB
-- the same order as the physical limits -- stops competing with them.

## 5. Cost

`get_xtc_freq` takes ~75 ms against ~9 ms for the integer path: the FFT frame is
16× larger (131072 points, sized so that the full linear convolution does not
wrap). Irrelevant in an offline generator that runs once. If it ever mattered,
the frame could drop to 65536 with no loss: the deepest term of the ladder is
2.8e-18, below double-precision epsilon relative to the unit delta.

## 6. Outstanding

- `compare_frac_delay.py` only measures the symmetric case. The asymmetric one
  is verified by equivalence with the symmetric one (`make check`), not by direct
  measurement against an asymmetric plant.
- Not exposed in the VST3 port.

### An unrelated divergence, to be reported to the port's author

`Source/DSP/NatAmbio/XtcFilterDesigner.cpp:266` uses `fftSize = n` in
`minimumPhase()`, without the ×8 cepstral oversampling that `lib/dsp.c:55` and
the Python port do have. It is exactly the ~0.2 dB error documented in
`lib/dsp.c:157-175`, amplified to ~5 % through the 16 chained convolutions. It
has nothing to do with the ITD, but it is worth fixing before comparing
anything.
