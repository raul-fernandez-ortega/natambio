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

Usage:
    python3 test_xtc_sweep_offline.py track.wav [--secs 2] [--ov 0.1]
"""

import argparse
import os

import numpy as np
import soundfile as sf

# ---- SEQUENCE: (gain_dB, channel) ------------------------------------------
# Ascending levels: -40..0 dB in steps of 5, and 0..+6 dB in steps of 0.5.
# The sweep rises on L (-40 -> +6), jumps to R at +6 and falls (+6 -> -40).
_LEVELS = [float(g) for g in range(-40, 1, 5)] \
        + [round(0.5 * i, 1) for i in range(1, 13)]         # ... -5, 0, 0.5 ... 6.0
STEPS = [(g, 'L') for g in _LEVELS] + [(g, 'R') for g in reversed(_LEVELS)]
# ----------------------------------------------------------------------------

BLOCK = 1024        # processing block (offline; only affects speed, not output)


def out_name(path):
    root, ext = os.path.splitext(path)
    return root + "_sweep" + (ext or ".wav")


def main():
    ap = argparse.ArgumentParser(description="XTC sweep, offline render to WAV.")
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

    out = np.zeros((pass_frames, 2), dtype=np.float32)

    # State (mirrors the realtime callback of test_xtc_sweep.py).
    curL = curR = 0.0
    tL = tR = 0.0
    dL = dR = 0.0
    ramp = 0
    last_k = -1
    mpos = 0
    pos = 0

    while pos < pass_frames:
        k = pos // step_frames
        boundary = (k + 1) * step_frames
        n = min(BLOCK, pass_frames - pos, boundary - pos)

        if k != last_k and k < len(STEPS):
            g, ch = STEPS[k]
            factor = 10.0 ** (g / 20.0)
            tL = factor if ch == 'L' else 0.0
            tR = factor if ch == 'R' else 0.0
            dL = (tL - curL) / ov_frames
            dR = (tR - curR) / ov_frames
            ramp = ov_frames
            last_k = int(k)

        # Per-sample gain vectors (linear ramp -> plateau).
        gL = np.empty(n, dtype=np.float32)
        gR = np.empty(n, dtype=np.float32)
        if ramp > 0:
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

        # Music block (continuous, wraps at the WAV end).
        m = (mpos + np.arange(n)) % N
        L = wav[m, 0]
        R = wav[m, 1]
        out[pos:pos + n, 0] = L - gL * L
        out[pos:pos + n, 1] = R - gR * R

        mpos = (mpos + n) % N
        pos += n

    dst = out_name(args.wav)
    sf.write(dst, out, sr, subtype="FLOAT")
    dur = pass_frames / sr
    print(f"Wrote {dst}  ({len(STEPS)} steps x {args.secs}s = {dur:.1f}s, {sr} Hz)")


if __name__ == "__main__":
    main()
