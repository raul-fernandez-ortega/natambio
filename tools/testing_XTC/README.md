# testing_XTC

These tools are the measurement and listening tests used during the development
of NatAmbio's XTC filters.

They are intentionally published before the accompanying perceptual analysis, so
that anyone can reproduce the experiments independently.

Test-signal generators for evaluating cross-talk cancellation (XTC) and
inter-aural cue perception. Each tool sweeps a controlled cue across a stereo
pair while music plays, so you can listen to how the sweet spot and the
phantom image respond.

There are **two algorithms**, each shipped in a **realtime** variant (a native
JACK client) and an **offline** variant (file-to-file, no JACK — runs on
Windows too):

| Algorithm | Realtime (JACK) | Offline (WAV out) |
|-----------|-----------------|-------------------|
| Inversion & sum | `test_xtc_sweep.py` | `test_xtc_sweep_offline.py` |
| ITD/ILD parametric | `test_xtc_ild_itd_sweep.py` | `test_xtc_ild_itd_sweep_offline.py` |

All four downmix the input to **mono** first (see *Input signal* below), so the
two output channels start out identical and differ **only** by the effect being
swept.

---

## What these tools are for

Although they were written to develop and validate cross-talk cancellation
(XTC), these scripts apply **perfectly well to conventional stereo systems**.
Running them **before** any XTC processing lets you gauge the baseline
**sound-stage width** of a plain stereo setup: how far the phantom image can be
pushed toward (and beyond) each speaker purely with level and time cues. That
baseline is the reference against which the improvement brought by XTC should be
judged.

When the test signals become **inverted / anti-correlated** — the over-cancel
region of algorithm 1 (gains above 0 dB, where `1 - g` goes negative) and the
inverted phases of algorithm 2 — you can hear the **de-localisation effect**:
the image loses a definite position, spreads, and appears to detach from the
speakers. This is a useful probe of how the room and the listening position
handle out-of-phase content, and of how robust (or fragile) the phantom image
is once inter-channel correlation breaks down.

Testing the anti-correlated region is **optional**: every tool takes a
**phase mode** (`--phase` in the scripts, the *In-phase only* control port in
the LADSPA plugins). The default, `both`, tests **in phase and with phase
inversion**; `in` restricts the sweep to **in-phase only**, so the two channels
never become anti-correlated. That lets you gauge the plain sound-stage width
first and only then bring in the de-localisation effect.

In both algorithms the swept parameter produces a **moving virtual source**: as
the sweep advances, the perceived localisation glides across the sound stage —
travelling from one side to the other and, in the anti-correlated regions,
dissolving and re-forming. Listening to that motion, rather than to a single
static setting, is what makes the spatial behaviour of the system easy to
evaluate by ear.

---

## Algorithm 1 — Inversion & sum

On each step the input goes to both channels, and on the *active* channel an
**inverted, attenuated copy of that same channel** is summed in:

```
left  = L - gL * L   (= L * (1 - gL))
right = R - gR * R   (= R * (1 - gR))
```

`gL` / `gR` are **linear** gain factors, `10**(dB/20)`; the inactive channel
keeps a gain of 0 (untouched). At `g = 1` (0 dB) the channel is fully
cancelled; below 0 dB it is partially cancelled; above 0 dB it over-cancels and
flips polarity.

**Sweep / loop.** The ladder is set in **attenuation of the named channel**,
not in `g`, so the rungs are perceptually even and the image keeps moving:

- **In-phase leg, 9 steps per side**: `0, -3, -6, -9, -12, -15, -18, -21 dB`,
  then **MUTE** (infinite attenuation) — the widest panning there is, the named
  channel goes silent and everything arrives from the other one.
- **Over-cancel leg, 12 more steps per side** (`--phase both` only): past mute
  the channel comes back **phase-inverted**, from -24.5 dB up to full level
  (`g` = +0.5 … +6.0 dB in 0.5 dB steps).
- The ladder runs **down on L**, turns at the far end and comes back **up on
  R**. In loop mode it starts over at the top of the L ladder. The **music
  keeps flowing** across passes; only the **sweep** restarts.

`g` is derived per step: `g = 1 - 10^(-A/20)` for an attenuation `A`, and
`g = 1` (full cancellation) for mute.

**Phase mode.** `both` (default) is the whole thing, 21 steps per side, 42 in
all. `in` ends each side at mute: 9 steps per side, 18 in all, and the two mute
steps meet in the middle — hard pan one way, then hard pan the other, which
also marks the turnaround by ear.

Between steps there is a sample-by-sample **crossfade** (default 0.1 s) so gain
changes and the L↔R handover are click-free.

---

## Algorithm 2 — ITD/ILD parametric

Instead of summing an inverted copy, the *active* channel is **delayed** and
**attenuated** as a function of an angle `z` (degrees); the other channel is
pass-through:

```
active channel = attenuate( delay( that channel ) )
other  channel = pass-through
```

The delay needs no separate delay line: the WAV loops in RAM, so a delay of `D`
samples is just reading that channel at `(position - D)`.

Per-step laws (`z` in degrees):

```
delay (us) = 5.674*z + 184.131*sin(z)        -> rounded to integer samples  (ITD)
atten (dB) = -0.10 + 0.407*z - 0.0025*z^2     -> applied as attenuation       (ILD)
```

**Sweep / loop.** The angle `z` runs in **+5° steps** through four phases, and
the *invert* flag flips the polarity of the active channel:

1. **L**, `z` 0 → 90° (normal)
2. **L**, `z` 90 → 0° (inverted)
3. **R**, `z` 0 → 90° (normal)
4. **R**, `z` 90 → 0° (inverted)

In loop mode it starts over at phase 1. As with algorithm 1, between steps a
sample-by-sample crossfade keeps the delay change, the level change and the
L↔R handover click-free.

**Phase mode.** `both` (default) is the sequence above. `in` simply clears the
*invert* flag of phases 2 and 4: the sweep keeps the same motion — out to 90°
and back on each side — but never flips polarity. The step count is the same
(76) in both modes.

---

## Reading the step messages

Every tool announces each step — the realtime scripts and the LADSPA plugins as
the sweep advances, the offline scripts as a cue sheet of time offsets printed
before rendering:

```
  0:06.0 | step  4/18 | L =  -9.0 dB vs R, in phase | image -> R
  0:16.0 | step  9/18 | L MUTED (-inf dB vs R) | image -> R hard
  0:18.0 | step 10/18 | R MUTED (-inf dB vs L) | image -> L hard
  0:20.0 | step 11/18 | R = -21.0 dB vs L, in phase | image -> L
```

The dB figure is what is **left of the named channel** relative to the
untouched one — the thing you hear, not the internal gain. And there is one
thing worth getting straight before listening, because it reads backwards at
first: **the named channel is the one being removed**, so the image moves to
the *other* side, and the widest panning is at **MUTE**.

In algorithm 2 the named channel is the one being **delayed and attenuated**,
so the image again moves to the other side. There the printed `atten` is a
genuine attenuation of that channel, and the message states its resulting level
relative to the untouched one (minus that figure).

---

## Input signal (mono downmix)

Before any processing, **all four scripts collapse the input to mono** and feed
that same signal to both channels:

```
mono  = (L + R) / 2
L = R = mono
```

This guarantees both output channels start from an identical source, so any
audible or measurable L/R difference is produced purely by the sweep. Mono
inputs are accepted (duplicated to stereo first); files with more than two
channels keep only the first two before the downmix.

The sample rate is taken from the WAV. The realtime scripts require the WAV
rate to match the running JACK server; the offline scripts impose no such
constraint.

---

## Parameters

### Realtime scripts (`test_xtc_sweep.py`, `test_xtc_ild_itd_sweep.py`)

```
python3 <script>.py track.wav [--secs 2.0] [--ov 0.1] [--repeat 1|inf]
                              [--phase both|in]
```

| Argument | Default | Meaning |
|----------|---------|---------|
| `track.wav` | — | input WAV (positional) |
| `--secs` | `2.0` | seconds per step |
| `--ov` | `0.1` | overlap / crossfade duration (s); must be shorter than `--secs` |
| `--repeat` | `1` | number of sweep passes, or `inf` for endless |
| `--phase` | `both` | `both`: test in phase **and** phase-inverted; `in`: in-phase only |

JACK client name and the output/destination port names are defined as
variables at the top of each realtime script (`CLIENT_NAME`, `OUT_LEFT`,
`OUT_RIGHT`, `DEST_LEFT`, `DEST_RIGHT`) — edit them to match your graph. By
default they connect to `natambio:front_input_left` / `..._right`.

### Offline scripts (`test_xtc_sweep_offline.py`, `test_xtc_ild_itd_sweep_offline.py`)

```
python3 <script>_offline.py track.wav [--secs 2.0] [--ov 0.1] [--phase both|in]
```

Same `--secs` / `--ov` / `--phase` as above. They render **exactly one** sweep
pass and write it next to the input WAV:

| Script | `--phase both` (default) | `--phase in` |
|--------|--------------------------|--------------|
| `test_xtc_sweep_offline.py` | `<input>_sweep.wav` | `<input>_sweep_inphase.wav` |
| `test_xtc_ild_itd_sweep_offline.py` | `<input>_ildsweep.wav` | `<input>_ildsweep_inphase.wav` |

The two phase modes write to **different names**, so an in-phase render never
overwrites a full one.

Before rendering, both print a **cue sheet**: one line per step with its time
offset in the output file, so you can jump straight to the step you want to
hear.

Output is 32-bit float WAV (avoids clipping). There is no `--repeat`: offline
always produces a single pass.

---

## Requirements

- Realtime: `numpy`, `soundfile`, `JACK-Client` (`pip install JACK-Client`) and
  a running JACK server.
- Offline: `numpy`, `soundfile` only — no JACK, cross-platform (Windows/macOS/Linux).
