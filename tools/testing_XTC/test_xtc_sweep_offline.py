#!/usr/bin/env python3
"""
Offline (file -> file) version of test_xtc_sweep.py. No JACK, no realtime.

Renders ONE full sweep pass and writes it next to the input WAV as
<input>_sweep.wav. Cross-platform (only numpy + soundfile), so it also runs
on Windows where JACK is not available.

For each step it sends the WAV to both channels and, on the step's channel,
SUMS an inverted, attenuated copy of THAT channel:

    left  = L - gL * L      (= L*(1-gL))
    right = R - gR * R      (= R*(1-gR))

gL/gR are LINEAR factors (10**(dB/20)); the unused channel keeps its 0 gain.
Between steps there is an 'overlap'-second sample-by-sample crossfade. The
music advances continuously and wraps at the WAV end (loops within the pass).

With --phase you choose whether the sweep visits the phase-inverted region:
'both' (default) runs the whole ladder, in phase down to mute and then back
up with the named channel inverted; 'in' keeps everything in phase, ending
each side at mute. The
in-phase-only render is written as <input>_sweep_inphase.wav.

Usage:
    python3 test_xtc_sweep_offline.py track.wav [--secs 2] [--ov 0.1]
                                                [--phase both|in]
"""

import argparse
import math
import os

import numpy as np
import soundfile as sf

# ---- SEQUENCE: (remaining factor, channel) ---------------------------------
# A step is stored as the SIGNED FACTOR LEFT on the named channel, which is
# what you actually hear: 1.0 = untouched, 0.5 = -6 dB, 0.0 = muted, negative
# = the channel comes back phase-inverted. The gain of the inverted copy that
# gets summed in is g = 1 - rem.
PHASE_MODES = ("both", "in")

# In-phase leg: attenuation of the named channel, 0 -> 21 dB in 3 dB steps,
# then mute. Even rungs in ATTENUATION keep the image moving; a ladder even
# in g crowds most of its steps within a dB of the centre.
IN_ATT_DB = [3.0 * i for i in range(8)]                  # 0, 3, 6 ... 21
# Inverted leg ('both' only): past mute the named channel comes back with its
# polarity flipped, -14 dB up to full level in 2 dB steps. Still the
# over-cancel region (g > 0 dB), but spaced by what is left of the channel
# instead of by g.
INV_ATT_DB = [14.0 - 2.0 * i for i in range(8)]          # 14, 12 ... 0


def build_steps(phase="both"):
    """Step sequence (remaining factor, channel) for the requested phase mode.

    Per side: the in-phase ladder 0, -3, -6 ... -21 dB, then MUTE — the
    widest panning there is, the named channel goes silent and everything
    comes from the other one. With phase 'both' the inverted ladder follows,
    the same channel coming back with its polarity flipped, -14 dB up to
    full level in 2 dB steps.

    The ladder runs down on L and back up on R, which walks the four phases
    in order: in phase L, inverted L, inverted R, in phase R. The turn sits
    at full level inverted, where the two sides are the same signal up to an
    overall polarity flip, so the handover is seamless. In 'in' mode there is
    no inverted leg and the two mute steps meet instead: hard pan one way,
    then hard pan the other.

    Ascending levels: -40..0 dB in steps of 5, and (phase 'both' only)
    0..+6 dB in steps of 0.5. The sweep rises on L and falls on R.

    'both' includes the over-cancel region above 0 dB, where (1 - g) goes
    negative and the active channel comes out phase-INVERTED.
    'in' stops at 0 dB (full cancellation), so the output never becomes
    anti-correlated: everything stays in phase.
    """
    rem = [10.0 ** (-a / 20.0) for a in IN_ATT_DB]     # 1.0, 0.708 ... 0.089
    rem.append(0.0)                                    # mute (-inf dB)
    if phase == "both":
        rem += [-(10.0 ** (-a / 20.0))                 # negative -> inverted
                for a in INV_ATT_DB]
    return [(r, 'L') for r in rem] + [(r, 'R') for r in reversed(rem)]
# ----------------------------------------------------------------------------


def step_label(rem, ch, k, n):
    """One step in plain words: what is left of the named channel, and where
    the image goes.

    The named channel is the one being CANCELLED (an inverted copy of it is
    summed into it), so the image moves to the OTHER side, and the widest
    panning is at MUTE rather than at either end of the dB ladder.
    """
    other = 'R' if ch == 'L' else 'L'
    head = f"step {k + 1:2d}/{n}"
    if rem == 0.0:
        return f"{head} | {ch} MUTED (-inf dB vs {other}) | image -> {other} hard"
    eff = 20.0 * math.log10(abs(rem))      # named ch level vs the untouched one
    if rem > 0.0:
        pol = "in phase"
        img = "image: centre" if eff > -1.0 else f"image -> {other}"
    else:
        pol = "INVERTED"
        img = (f"image -> {other} (anti-corr.)" if eff <= -12.0
               else "image: de-localised")
    return f"{head} | {ch} = {eff:+5.1f} dB vs {other}, {pol} | {img}"


# A crossfade cannot get from a signal to its exact inverse without passing
# through silence, so the one junction where that happens is switched hard.
ZC_MAX_S = 0.05        # give up waiting for a zero crossing after this long


def is_polarity_flip(a, b):
    """True when two steps put out exactly opposite signals.

    Both sides at full level inverted — (-mono, +mono) and (+mono, -mono) —
    are the same sound with the polarity of the whole signal flipped, which
    is inaudible. Crossfading between them would take both channels through
    zero, a hole as long as the crossfade, so the sweep switches instantly
    at a zero crossing instead. It is the only such junction in a pass: the
    other repeated step, the centre at the loop seam, has both gains at 0
    and nothing to fade.
    """
    return a[0] == -1.0 and b[0] == -1.0 and a[1] != b[1]


def zero_crossing(prev, blk, waited, limit):
    """First sample of blk whose sign differs from the one before it, or the
    last sample once 'limit' samples have gone by; None while still waiting."""
    s = np.signbit(np.concatenate(([prev], blk)))
    idx = np.nonzero(s[:-1] != s[1:])[0]
    if idx.size:
        return int(idx[0])
    return len(blk) - 1 if waited + len(blk) >= limit else None

BLOCK = 1024        # processing block (offline; only affects speed, not output)


def out_name(path, phase="both"):
    root, ext = os.path.splitext(path)
    tag = "_sweep" if phase == "both" else "_sweep_inphase"
    return root + tag + (ext or ".wav")


def main():
    ap = argparse.ArgumentParser(description="XTC sweep, offline render to WAV.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    ap.add_argument("--phase", choices=PHASE_MODES, default="both",
                    help="'both' (default): sweep in phase AND through the "
                         "phase-inverted (over-cancel) region above 0 dB; "
                         "'in': in-phase only, stop at 0 dB")
    args = ap.parse_args()

    STEPS = build_steps(args.phase)

    wav, sr = sf.read(args.wav, dtype="float32", always_2d=True)
    if wav.shape[1] == 1:               # mono -> duplicate to stereo
        wav = np.repeat(wav, 2, axis=1)
    elif wav.shape[1] > 2:
        wav = wav[:, :2]
    # Downmix to mono ((L+R)/2) and feed the SAME signal to both L and R.
    mono = 0.5 * (wav[:, 0] + wav[:, 1])
    wav = np.stack((mono, mono), axis=1)
    N = wav.shape[0]

    step_frames = int(round(args.secs * sr))
    ov_frames = max(1, int(round(args.ov * sr)))
    zc_max = max(1, int(round(ZC_MAX_S * sr)))
    if ov_frames >= step_frames:
        raise SystemExit("Overlap must be shorter than the step duration.")
    pass_frames = step_frames * len(STEPS)

    out = np.zeros((pass_frames, 2), dtype=np.float32)

    # Cue sheet: where each step starts in the rendered file, and what it does.
    dst = out_name(args.wav, args.phase)
    print(f"{len(STEPS)} steps x {args.secs}s, overlap {args.ov}s, "
          f"phase={args.phase} -> {dst}")
    for k, step in enumerate(STEPS):
        t0 = k * step_frames / sr
        print(f"  {int(t0) // 60:d}:{t0 % 60:04.1f} | "
              f"{step_label(step[0], step[1], k, len(STEPS))}")

    # State (mirrors the realtime callback of test_xtc_sweep.py).
    curL = curR = 0.0
    tL = tR = 0.0
    dL = dR = 0.0
    ramp = 0
    flip = wait = 0        # polarity flip pending a zero crossing
    prev_mono = 0.0
    last_k = -1
    mpos = 0
    pos = 0

    while pos < pass_frames:
        k = pos // step_frames
        boundary = (k + 1) * step_frames
        n = min(BLOCK, pass_frames - pos, boundary - pos)

        if k != last_k and k < len(STEPS):
            step = STEPS[k]
            prev = STEPS[last_k] if last_k >= 0 else (1.0, step[1])
            factor = 1.0 - step[0]      # gain of the inverted copy to sum in
            tL = factor if step[1] == 'L' else 0.0
            tR = factor if step[1] == 'R' else 0.0
            last_k = int(k)
            if is_polarity_flip(prev, step):
                flip, wait, ramp = 1, 0, 0
            else:
                dL = (tL - curL) / ov_frames
                dR = (tR - curR) / ov_frames
                ramp = ov_frames

        # Music block (continuous, wraps at the WAV end). Read before the
        # gains, so a pending polarity flip can find its zero crossing in it.
        m = (mpos + np.arange(n)) % N
        L = wav[m, 0]
        R = wav[m, 1]

        # Per-sample gain vectors (linear ramp -> plateau, or a hard switch).
        gL = np.empty(n, dtype=np.float32)
        gR = np.empty(n, dtype=np.float32)
        if flip:
            j = zero_crossing(prev_mono, L, wait, zc_max)
            if j is None:                      # not yet: hold the old step
                wait += n
                gL[:] = curL
                gR[:] = curR
            else:                              # switch here, no ramp at all
                gL[:j], gR[:j] = curL, curR
                curL, curR = tL, tR
                gL[j:], gR[j:] = curL, curR
                flip = 0
        elif ramp > 0:
            r = min(ramp, n)
            idx = np.arange(1, r + 1, dtype=np.float32)
            gL[:r] = curL + dL * idx
            gR[:r] = curR + dR * idx
            curL += dL * r
            curR += dR * r
            ramp -= r
            if ramp == 0:
                curL, curR = tL, tR
            if r < n:
                gL[r:] = curL
                gR[r:] = curR
        else:
            gL[:] = curL
            gR[:] = curR
        prev_mono = float(L[-1])

        out[pos:pos + n, 0] = L - gL * L
        out[pos:pos + n, 1] = R - gR * R

        mpos = (mpos + n) % N
        pos += n

    sf.write(dst, out, sr, subtype="FLOAT")
    dur = pass_frames / sr
    print(f"Wrote {dst}  ({len(STEPS)} steps x {args.secs}s = {dur:.1f}s, "
          f"{sr} Hz, phase={args.phase})")


if __name__ == "__main__":
    main()
