#!/usr/bin/env python3
"""
Continuous ILD/ITD sweep, implemented as a native JACK client (no ecasound).

Unlike test_xtc_sweep.py (which SUMS an inverted attenuated copy onto one
channel), here the "modified" channel is DELAYED and ATTENUATED as a function
of an angle z (degrees):

    modified channel = attenuate( delay( that channel ) )
    other    channel = pass-through (untouched)

The delay is obtained without a separate delay line: the whole WAV lives in RAM
and loops, so a delay of D samples is just reading that channel at (mpos - D).

Per-step laws (z in degrees):
    delay (us) = 5.674*z + 184.131*sin(z)          -> rounded to integer samples
    atten (dB) = -0.10 + 0.407*z - 0.0025*z^2       -> applied as attenuation

z runs 0 -> 90 deg on L, then 90 -> 0 deg on R, in +5 deg steps; in loop mode
it starts over. Between steps there is a sample-by-sample crossfade so both the
delay change and the level change are click-free (and so is the L<->R handover).
The music advances continuously and wraps at the WAV end; with --repeat only the
SWEEP (the step sequence) restarts, the MUSIC keeps flowing.

With --phase you choose whether the falling legs are phase-inverted: 'both'
(default) tests in phase and with polarity flipped, 'in' keeps the same sweep
motion entirely in phase.

Usage:
    python3 test_xtc_ild_itd_sweep.py track.wav [--secs 2] [--ov 0.1]
                                                [--repeat 1|inf] [--phase both|in]
"""

import argparse
import math
import threading

import numpy as np
import soundfile as sf
import jack

# ---- JACK PORTS (edit to match your setup) ---------------------------------
# Name of this JACK client.
CLIENT_NAME = "xtc_ild_itd_sweep"
# This client's own output ports (created by the script).
OUT_LEFT = "front_left"
OUT_RIGHT = "front_right"
# Destination ports these outputs connect to (where the audio is sent).
# Change these to whatever input ports correspond in your JACK graph.
DEST_LEFT = "natambio:front_input_left"
DEST_RIGHT = "natambio:front_input_right"
# ----------------------------------------------------------------------------

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


def main():
    ap = argparse.ArgumentParser(description="ILD/ITD sweep as a JACK client.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    ap.add_argument("--repeat", default="1", help="number of passes or 'inf'")
    ap.add_argument("--phase", choices=PHASE_MODES, default="both",
                    help="'both' (default): test in phase AND with the "
                         "modified channel phase-inverted on the falling "
                         "legs; 'in': in-phase only, never invert")
    args = ap.parse_args()

    repeat = float("inf") if args.repeat == "inf" else int(args.repeat)
    STEPS = build_steps(args.phase)

    # Whole WAV loaded into RAM (no disk access inside the callback).
    wav, sr = sf.read(args.wav, dtype="float32", always_2d=True)
    if wav.shape[1] == 1:               # mono -> duplicate to stereo
        wav = np.repeat(wav, 2, axis=1)
    elif wav.shape[1] > 2:
        wav = wav[:, :2]
    # Downmix to mono ((L+R)/2) and feed the SAME signal to both L and R.
    mono = 0.5 * (wav[:, 0] + wav[:, 1])
    wav = np.stack((mono, mono), axis=1)

    client = jack.Client(CLIENT_NAME)
    if sr != client.samplerate:
        raise SystemExit(
            f"WAV sample rate ({sr}) != JACK ({client.samplerate}). "
            f"Resample the WAV or adjust JACK."
        )

    step_frames = int(round(args.secs * sr))
    ov_frames = max(1, int(round(args.ov * sr)))
    pass_frames = step_frames * len(STEPS)
    if ov_frames >= step_frames:
        raise SystemExit("Overlap must be shorter than the step duration.")

    # Precompute per-step params: (delay in samples, linear atten factor, channel).
    # atten_db is an ATTENUATION magnitude in dB -> linear factor = 10**(-dB/20).
    PARAMS = []
    for z, ch, inv in STEPS:
        d = int(round(delay_us(z) * 1e-6 * sr))
        a = 10.0 ** (-atten_db(z) / 20.0)
        if inv:
            a = -a                                  # polarity flip of modified ch
        PARAMS.append((d, a, ch))

    outL = client.outports.register(OUT_LEFT)
    outR = client.outports.register(OUT_RIGHT)

    done = threading.Event()

    # State shared with the callback (simple types / floats only).
    # cur* = settled params of the modified path; old* = params being faded out.
    st = {
        "pos": 0,           # frame within the current pass (drives the SWEEP)
        "mpos": 0,          # WAV read frame (drives the MUSIC; wraps around)
        "passno": 0,        # pass number (0-indexed)
        "last_k": -1,       # last step triggered in this pass
        "curD": 0, "curA": 1.0, "curCh": STEPS[0][1],   # current (target)
        "oldD": 0, "oldA": 1.0, "oldCh": STEPS[0][1],   # previous (fading out)
        "ramp": 0,          # remaining crossfade samples
        "xruns": 0,
    }

    N = wav.shape[0]

    @client.set_xrun_callback
    def _xrun(delay):
        st["xruns"] += 1

    def chan_signal(m, letter, ci, mod_ch, D, A):
        """Per-channel output: delayed+attenuated if this is the modified
        channel, otherwise pass-through."""
        if letter == mod_ch:
            return A * wav[(m - D) % N, ci]
        return wav[m, ci]

    @client.set_process_callback
    def process(frames):
        bL = outL.get_array()
        bR = outR.get_array()

        if done.is_set():
            bL[:] = 0.0
            bR[:] = 0.0
            return

        pos = st["pos"]
        n = frames

        # Time to trigger a new step? (at block granularity; the crossfade
        # itself is sample by sample)
        k = pos // step_frames
        if k != st["last_k"] and k < len(STEPS):
            D, A, ch = PARAMS[k]
            st["oldD"], st["oldA"], st["oldCh"] = st["curD"], st["curA"], st["curCh"]
            st["curD"], st["curA"], st["curCh"] = D, A, ch
            st["ramp"] = ov_frames
            st["last_k"] = int(k)

        # Crossfade weight t: 0 -> 1 over ov_frames, then stays at 1.
        rem = st["ramp"]
        if rem > 0:
            start = ov_frames - rem
            t = np.minimum(1.0, (start + np.arange(1, n + 1, dtype=np.float32)) / ov_frames)
            st["ramp"] = max(0, rem - n)
        else:
            t = None                                # fully settled -> new only

        # Music read positions for this block (continuous, wraps at WAV end).
        mpos = st["mpos"]
        m = (mpos + np.arange(n)) % N

        for ci, letter, buf in ((0, 'L', bL), (1, 'R', bR)):
            new_sig = chan_signal(m, letter, ci,
                                  st["curCh"], st["curD"], st["curA"])
            if t is None:
                buf[:] = new_sig
            else:
                old_sig = chan_signal(m, letter, ci,
                                      st["oldCh"], st["oldD"], st["oldA"])
                buf[:] = (1.0 - t) * old_sig + t * new_sig

        st["mpos"] = (mpos + n) % N

        # Advance the SWEEP / handle end of pass (the music does NOT restart).
        new_pos = pos + n
        if new_pos >= pass_frames:
            if st["passno"] + 1 < repeat:
                st["passno"] += 1
                st["last_k"] = -1
                new_pos = 0                          # restart the sweep only
            else:
                done.set()
        st["pos"] = new_pos

    try:
        with client:
            client.connect(outL, DEST_LEFT)
            client.connect(outR, DEST_RIGHT)
            print(f"JACK {client.samplerate} Hz, block {client.blocksize} | "
                  f"{len(STEPS)} steps x {args.secs}s, overlap {args.ov}s, "
                  f"repeat={args.repeat}, phase={args.phase}")
            last_print = -1
            while not done.wait(timeout=0.05):
                k = st["last_k"]
                if k != last_print and 0 <= k < len(STEPS):
                    z, ch, inv = STEPS[k]
                    D, _, _ = PARAMS[k]
                    print(f">> {step_label(z, ch, inv, D)}   "
                          f"(pass {st['passno'] + 1})")
                    last_print = k
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        if st["xruns"]:
            print(f"xruns: {st['xruns']}")


if __name__ == "__main__":
    main()
