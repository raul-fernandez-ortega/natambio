# testing_XTC

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

**Sweep / loop.** The step sequence walks the gain and switches channels:

- Ascending levels: **-40 → 0 dB in 5 dB steps**, then **0 → +6 dB in 0.5 dB
  steps**.
- The sweep **rises on L** (-40 → +6 dB), **jumps to R** at +6 dB, and **falls
  on R** (+6 → -40 dB).
- In loop mode it starts over at -40 dB on L. The **music keeps flowing**
  across passes; only the **sweep** restarts.

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
```

| Argument | Default | Meaning |
|----------|---------|---------|
| `track.wav` | — | input WAV (positional) |
| `--secs` | `2.0` | seconds per step |
| `--ov` | `0.1` | overlap / crossfade duration (s); must be shorter than `--secs` |
| `--repeat` | `1` | number of sweep passes, or `inf` for endless |

JACK client name and the output/destination port names are defined as
variables at the top of each realtime script (`CLIENT_NAME`, `OUT_LEFT`,
`OUT_RIGHT`, `DEST_LEFT`, `DEST_RIGHT`) — edit them to match your graph. By
default they connect to `natambio:front_input_left` / `..._right`.

### Offline scripts (`test_xtc_sweep_offline.py`, `test_xtc_ild_itd_sweep_offline.py`)

```
python3 <script>_offline.py track.wav [--secs 2.0] [--ov 0.1]
```

Same `--secs` / `--ov` as above. They render **exactly one** sweep pass and
write it next to the input WAV:

- `test_xtc_sweep_offline.py`        → `<input>_sweep.wav`
- `test_xtc_ild_itd_sweep_offline.py` → `<input>_ildsweep.wav`

Output is 32-bit float WAV (avoids clipping). There is no `--repeat`: offline
always produces a single pass.

---

## Requirements

- Realtime: `numpy`, `soundfile`, `JACK-Client` (`pip install JACK-Client`) and
  a running JACK server.
- Offline: `numpy`, `soundfile` only — no JACK, cross-platform (Windows/macOS/Linux).
