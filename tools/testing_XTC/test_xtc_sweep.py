#!/usr/bin/env python3
"""
Continuous XTC sweep, implemented as a native JACK client (no ecasound).

A single player sends the WAV to the natambio input ports and, on the channel
of each step, SUMS an inverted, attenuated copy of THAT channel:

    front_input_left  = L - gL * L      (= L*(1-gL))
    front_input_right = R - gR * R      (= R*(1-gR))

gL/gR are LINEAR factors (10**(dB/20)); the unused channel goes to 0.
The sweep walks through STEPS; between steps there is a 'overlap'-second
crossfade done sample by sample inside the JACK callback. The music advances
continuously according to the frame counter and, when the WAV ends, wraps
around to the start (loop). With --repeat the MUSIC keeps advancing across
passes; only the SWEEP (the step sequence) restarts.

Usage:
    python3 test_xtc_sweep.py track.wav [--secs 10] [--ov 0.1] [--repeat 1|inf]
"""

import argparse
import threading

import numpy as np
import soundfile as sf
import jack

# ---- JACK PORTS (edit to match your setup) ---------------------------------
# Name of this JACK client.
CLIENT_NAME = "xtc_sweep"
# This client's own output ports (created by the script).
OUT_LEFT = "front_left"
OUT_RIGHT = "front_right"
# Destination ports these outputs connect to (where the audio is sent).
# Change these to whatever input ports correspond in your JACK graph.
DEST_LEFT = "natambio:front_input_left"
DEST_RIGHT = "natambio:front_input_right"
# ----------------------------------------------------------------------------

# ---- SEQUENCE: (gain_dB, channel) ------------------------------------------
# Ascending levels: -40..0 dB in steps of 5, and 0..+6 dB in steps of 0.5.
# The sweep rises on L (-40 -> +6), jumps to R at +6 and falls (+6 -> -40);
# in loop mode, after -40 R it starts over at -40 L.
_LEVELS = [float(g) for g in range(-40, 1, 5)] \
        + [round(0.5 * i, 1) for i in range(1, 13)]         # ... -5, 0, 0.5 ... 6.0
STEPS = [(g, 'L') for g in _LEVELS] + [(g, 'R') for g in reversed(_LEVELS)]
# ----------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description="XTC sweep as a JACK client.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    ap.add_argument("--repeat", default="1", help="number of passes or 'inf'")
    args = ap.parse_args()

    repeat = float("inf") if args.repeat == "inf" else int(args.repeat)

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

    outL = client.outports.register(OUT_LEFT)
    outR = client.outports.register(OUT_RIGHT)

    done = threading.Event()

    # State shared with the callback (simple types / floats only).
    st = {
        "pos": 0,          # frame within the current pass (drives the SWEEP)
        "mpos": 0,         # WAV read frame (drives the MUSIC; wraps around)
        "passno": 0,       # pass number (0-indexed)
        "last_k": -1,      # last step triggered in this pass
        "curL": 0.0, "curR": 0.0,   # linear gain currently applied
        "tL": 0.0, "tR": 0.0,       # crossfade target
        "dL": 0.0, "dR": 0.0,       # per-sample increment during the ramp
        "ramp": 0,         # remaining crossfade samples
        "xruns": 0,
    }

    @client.set_xrun_callback
    def _xrun(delay):
        st["xruns"] += 1

    @client.set_process_callback
    def process(frames):
        bL = outL.get_array()
        bR = outR.get_array()

        if done.is_set():
            bL[:] = 0.0
            bR[:] = 0.0
            return

        pos = st["pos"]

        # Time to trigger a new step? (at block granularity; the crossfade
        # itself is sample by sample)
        k = pos // step_frames
        if k != st["last_k"] and k < len(STEPS):
            g, ch = STEPS[k]
            factor = 10.0 ** (g / 20.0)
            tL = factor if ch == 'L' else 0.0
            tR = factor if ch == 'R' else 0.0
            st["tL"], st["tR"] = tL, tR
            st["dL"] = (tL - st["curL"]) / ov_frames
            st["dR"] = (tR - st["curR"]) / ov_frames
            st["ramp"] = ov_frames
            st["last_k"] = int(k)

        n = frames
        # Per-sample gain vectors (linear ramp -> plateau).
        gL = np.empty(n, dtype=np.float32)
        gR = np.empty(n, dtype=np.float32)
        rem = st["ramp"]
        if rem > 0:
            r = min(rem, n)
            idx = np.arange(1, r + 1, dtype=np.float32)
            gL[:r] = st["curL"] + st["dL"] * idx
            gR[:r] = st["curR"] + st["dR"] * idx
            st["curL"] += st["dL"] * r
            st["curR"] += st["dR"] * r
            rem -= r
            if rem == 0:                       # snap to the exact target
                st["curL"], st["curR"] = st["tL"], st["tR"]
            if r < n:
                gL[r:] = st["curL"]
                gR[r:] = st["curR"]
            st["ramp"] = rem
        else:
            gL[:] = st["curL"]
            gR[:] = st["curR"]

        # Audio chunk: the music advances continuously and, when the WAV
        # ends, wraps around to the start (loop).
        N = wav.shape[0]
        mpos = st["mpos"]
        end_m = mpos + n
        if end_m <= N:
            blk = wav[mpos:end_m]
        else:                                  # block crosses the end -> wrap
            blk = np.concatenate((wav[mpos:], wav[:end_m - N]))
        st["mpos"] = end_m % N
        L = blk[:, 0]
        R = blk[:, 1]
        bL[:] = L - gL * L
        bR[:] = R - gR * R

        # Advance the SWEEP / handle end of pass (the music does NOT restart).
        new_pos = pos + n
        if new_pos >= pass_frames:
            if st["passno"] + 1 < repeat:
                st["passno"] += 1
                st["last_k"] = -1
                new_pos = 0                    # restart the sweep only
            else:
                done.set()
        st["pos"] = new_pos

    try:
        with client:
            client.connect(outL, DEST_LEFT)
            client.connect(outR, DEST_RIGHT)
            print(f"JACK {client.samplerate} Hz, block {client.blocksize} | "
                  f"{len(STEPS)} steps x {args.secs}s, overlap {args.ov}s, "
                  f"repeat={args.repeat}")
            last_print = -1
            while not done.wait(timeout=0.05):
                k = st["last_k"]
                if k != last_print and 0 <= k < len(STEPS):
                    g, ch = STEPS[k]
                    print(f">> {g:+g} dB  {ch}   (pass {st['passno'] + 1})")
                    last_print = k
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        if st["xruns"]:
            print(f"xruns: {st['xruns']}")


if __name__ == "__main__":
    main()
