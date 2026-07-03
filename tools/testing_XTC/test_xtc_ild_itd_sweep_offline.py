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

Usage:
    python3 test_xtc_ild_itd_sweep_offline.py track.wav [--secs 2] [--ov 0.1]
"""

import argparse
import math
import os

import numpy as np
import soundfile as sf

# ---- SEQUENCE: (angle z in degrees, channel, invert) -----------------------
_UP = list(range(0, 91, 5))                        # 0, 5, 10, ... 90
_DOWN = list(reversed(_UP))                        # 90, 85, ... 0
STEPS = ([(z, 'L', False) for z in _UP]
         + [(z, 'L', True) for z in _DOWN]
         + [(z, 'R', False) for z in _UP]
         + [(z, 'R', True) for z in _DOWN])
# ----------------------------------------------------------------------------

BLOCK = 1024        # processing block (offline; only affects speed, not output)


def delay_us(z):
    """ITD-like delay in microseconds for angle z (degrees)."""
    return 5.674 * z + 184.131 * math.sin(math.radians(z))


def atten_db(z):
    """ILD-like attenuation in dB for angle z (degrees)."""
    return -0.10 + 0.407 * z - 0.0025 * z * z


def out_name(path):
    root, ext = os.path.splitext(path)
    return root + "_ildsweep" + (ext or ".wav")


def main():
    ap = argparse.ArgumentParser(description="ILD/ITD sweep, offline render to WAV.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    args = ap.parse_args()

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

    dst = out_name(args.wav)
    sf.write(dst, out, sr, subtype="FLOAT")
    dur = pass_frames / sr
    print(f"Wrote {dst}  ({len(STEPS)} steps x {args.secs}s = {dur:.1f}s, {sr} Hz)")


if __name__ == "__main__":
    main()
