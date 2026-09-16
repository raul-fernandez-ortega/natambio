#!/usr/bin/env python3
"""
Offline (file -> file) version of test_xtc_ild_itd_sweep.py. No JACK, no realtime.

Renders ONE full sweep pass and writes it next to the input WAV as
<input>_ildsweep.wav. Cross-platform (only numpy + soundfile), so it also runs
on Windows where JACK is not available.

The "modified" channel is DELAYED and ATTENUATED as a function of an angle z
(degrees); the other channel is pass-through:

    modified channel = attenuate( delay( that channel ) )
    other    channel = pass-through

The delay uses no separate delay line: the WAV loops in RAM, so a delay of D
samples is just reading that channel at (mpos - D).

Per-step laws (z in degrees):
    delay (us) = 5.674*z + 184.131*sin(z)          -> rounded to integer samples
    atten (dB) = -0.10 + 0.407*z - 0.0025*z^2       -> applied as attenuation

Four phases, +5 deg steps ('invert' flips the modified channel polarity):
    1) L, z 0..90  (normal)
    2) L, z 90..0  (inverted)
    3) R, z 0..90  (normal)
    4) R, z 90..0  (inverted)

Between steps there is a sample-by-sample crossfade (delay change, level change
and L<->R handover are click-free). The music wraps at the WAV end.

With --phase you choose whether the falling legs are phase-inverted: 'both'
(default) tests in phase and with polarity flipped, 'in' keeps the same sweep
motion entirely in phase and is written as <input>_ildsweep_inphase.wav.

Usage:
    python3 test_xtc_ild_itd_sweep_offline.py track.wav [--secs 2] [--ov 0.1]
                                                        [--phase both|in]
"""

import argparse
import math
import os

import numpy as np
import soundfile as sf

# ---- SEQUENCE: (angle z in degrees, channel, invert) -----------------------
PHASE_MODES = ("both", "in")


def build_steps(phase="both"):
    """Step sequence (z in degrees, channel, invert) for the phase mode.

    Four phases, in +5 deg steps ('invert' flips the modified channel
    polarity on the falling legs):
        1) L, z 0..90  (normal)
        2) L, z 90..0  (inverted with phase 'both', normal with 'in')
        3) R, z 0..90  (normal)
        4) R, z 90..0  (inverted with phase 'both', normal with 'in')

    'both' tests in phase AND phase-inverted; 'in' clears the invert flag,
    so the sweep keeps the same four-phase motion (out and back on each
    side) but never flips polarity.
    """
    up = list(range(0, 91, 5))                     # 0, 5, 10, ... 90
    down = list(reversed(up))                      # 90, 85, ... 0
    inv = (phase == "both")
    return ([(z, 'L', False) for z in up]
            + [(z, 'L', inv) for z in down]
            + [(z, 'R', False) for z in up]
            + [(z, 'R', inv) for z in down])
# ----------------------------------------------------------------------------

BLOCK = 1024        # processing block (offline; only affects speed, not output)


def delay_us(z):
    """ITD-like delay in microseconds for angle z (degrees)."""
    return 5.674 * z + 184.131 * math.sin(math.radians(z))


def atten_db(z):
    """ILD-like attenuation in dB for angle z (degrees)."""
    return -0.10 + 0.407 * z - 0.0025 * z * z


def step_label(z, ch, inv, d_smp):
    """One step in plain words: what it does, and where the image goes.

    The named channel is the one being DELAYED and ATTENUATED, so the image
    moves to the OTHER side; 'atten' is a real attenuation of the named
    channel (its level vs the untouched one is minus that figure).
    """
    other = 'R' if ch == 'L' else 'L'
    lvl = -atten_db(z)                     # named ch level vs the untouched one
    if inv:
        pol = "INVERTED"
        img = (f"image -> {other} (anti-corr.)" if lvl < -12.0
               else "image: de-localised")
    else:
        pol = "in phase"
        img = ("image: centre" if (lvl > -1.0 and d_smp == 0)
               else f"image -> {other}")
    return (f"z={z:>3} deg on {ch} | {ch} = {lvl:+5.1f} dB vs {other}, "
            f"delay {d_smp} smp/{delay_us(z):.1f} us, {pol} | {img}")


def out_name(path, phase="both"):
    root, ext = os.path.splitext(path)
    tag = "_ildsweep" if phase == "both" else "_ildsweep_inphase"
    return root + tag + (ext or ".wav")


def main():
    ap = argparse.ArgumentParser(description="ILD/ITD sweep, offline render to WAV.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    ap.add_argument("--phase", choices=PHASE_MODES, default="both",
                    help="'both' (default): test in phase AND with the "
                         "modified channel phase-inverted on the falling "
                         "legs; 'in': in-phase only, never invert")
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
    if ov_frames >= step_frames:
        raise SystemExit("Overlap must be shorter than the step duration.")
    pass_frames = step_frames * len(STEPS)

    # Precompute per-step params: (delay samples, linear atten factor, channel).
    # atten_db is an ATTENUATION magnitude in dB -> factor = 10**(-dB/20);
    # 'invert' negates that factor (polarity flip of the modified channel).
    PARAMS = []
    for z, ch, inv in STEPS:
        d = int(round(delay_us(z) * 1e-6 * sr))
        a = 10.0 ** (-atten_db(z) / 20.0)
        if inv:
            a = -a
        PARAMS.append((d, a, ch))

    out = np.zeros((pass_frames, 2), dtype=np.float32)

    # Cue sheet: where each step starts in the rendered file, and what it does.
    dst = out_name(args.wav, args.phase)
    print(f"{len(STEPS)} steps x {args.secs}s, overlap {args.ov}s, "
          f"phase={args.phase} -> {dst}")
    for k, step in enumerate(STEPS):
        t0 = k * step_frames / sr
        print(f"  {int(t0) // 60:d}:{t0 % 60:04.1f} | {step_label(step[0], step[1], step[2], PARAMS[k][0])}")

    def chan_signal(m, letter, ci, mod_ch, D, A):
        """Delayed+attenuated if this is the modified channel, else pass-through."""
        if letter == mod_ch:
            return A * wav[(m - D) % N, ci]
        return wav[m, ci]

    # State (mirrors the realtime callback).
    curD, curA, curCh = 0, 1.0, STEPS[0][1]
    oldD, oldA, oldCh = 0, 1.0, STEPS[0][1]
    ramp = 0
    last_k = -1
    mpos = 0
    pos = 0

    while pos < pass_frames:
        k = pos // step_frames
        boundary = (k + 1) * step_frames
        n = min(BLOCK, pass_frames - pos, boundary - pos)

        if k != last_k and k < len(STEPS):
            D, A, ch = PARAMS[k]
            oldD, oldA, oldCh = curD, curA, curCh
            curD, curA, curCh = D, A, ch
            ramp = ov_frames
            last_k = int(k)

        # Crossfade weight t: 0 -> 1 over ov_frames, then stays at 1.
        if ramp > 0:
            start = ov_frames - ramp
            t = np.minimum(1.0, (start + np.arange(1, n + 1, dtype=np.float32)) / ov_frames)
            ramp = max(0, ramp - n)
        else:
            t = None

        m = (mpos + np.arange(n)) % N
        for ci, letter in ((0, 'L'), (1, 'R')):
            new_sig = chan_signal(m, letter, ci, curCh, curD, curA)
            if t is None:
                out[pos:pos + n, ci] = new_sig
            else:
                old_sig = chan_signal(m, letter, ci, oldCh, oldD, oldA)
                out[pos:pos + n, ci] = (1.0 - t) * old_sig + t * new_sig

        mpos = (mpos + n) % N
        pos += n

    sf.write(dst, out, sr, subtype="FLOAT")
    dur = pass_frames / sr
    print(f"Wrote {dst}  ({len(STEPS)} steps x {args.secs}s = {dur:.1f}s, "
          f"{sr} Hz, phase={args.phase})")


if __name__ == "__main__":
    main()
