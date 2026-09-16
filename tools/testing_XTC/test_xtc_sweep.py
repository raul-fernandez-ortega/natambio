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

With --phase you choose whether the sweep visits the phase-inverted region:
'both' (default) runs the whole ladder, in phase down to mute and then back
up with the named channel inverted; 'in' keeps everything in phase, ending
each side at mute.

Usage:
    python3 test_xtc_sweep.py track.wav [--secs 10] [--ov 0.1] [--repeat 1|inf]
                                        [--phase both|in]
"""

import argparse
import math
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
    0..+6 dB in steps of 0.5. The sweep rises on L, jumps to R at the top
    and falls on R; in loop mode it starts over at -40 dB on L.

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


def main():
    ap = argparse.ArgumentParser(description="XTC sweep as a JACK client.")
    ap.add_argument("wav")
    ap.add_argument("--secs", type=float, default=2.0, help="seconds per step")
    ap.add_argument("--ov", type=float, default=0.1, help="overlap/crossfade (s)")
    ap.add_argument("--repeat", default="1", help="number of passes or 'inf'")
    ap.add_argument("--phase", choices=PHASE_MODES, default="both",
                    help="'both' (default): sweep in phase AND through the "
                         "phase-inverted (over-cancel) region above 0 dB; "
                         "'in': in-phase only, stop at 0 dB")
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
    zc_max = max(1, int(round(ZC_MAX_S * sr)))
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
        "flip": 0,         # polarity flip pending a zero crossing
        "wait": 0,         # samples spent waiting for it
        "prev_mono": 0.0,  # last input sample, for the sign test
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
            step = STEPS[k]
            prev = STEPS[st["last_k"]] if st["last_k"] >= 0 else (1.0, step[1])
            factor = 1.0 - step[0]      # gain of the inverted copy to sum in
            tL = factor if step[1] == 'L' else 0.0
            tR = factor if step[1] == 'R' else 0.0
            st["tL"], st["tR"] = tL, tR
            st["last_k"] = int(k)
            if is_polarity_flip(prev, step):
                st["flip"], st["wait"], st["ramp"] = 1, 0, 0
            else:
                st["dL"] = (tL - st["curL"]) / ov_frames
                st["dR"] = (tR - st["curR"]) / ov_frames
                st["ramp"] = ov_frames

        n = frames
        # Audio chunk: the music advances continuously and, when the WAV
        # ends, wraps around to the start (loop). Read before the gains, so
        # a pending polarity flip can look for its zero crossing in it.
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

        # Per-sample gain vectors (linear ramp -> plateau, or a hard switch).
        gL = np.empty(n, dtype=np.float32)
        gR = np.empty(n, dtype=np.float32)
        if st["flip"]:
            j = zero_crossing(st["prev_mono"], L, st["wait"], zc_max)
            if j is None:                      # not yet: hold the old step
                st["wait"] += n
                gL[:] = st["curL"]
                gR[:] = st["curR"]
            else:                              # switch here, no ramp at all
                gL[:j], gR[:j] = st["curL"], st["curR"]
                st["curL"], st["curR"] = st["tL"], st["tR"]
                gL[j:], gR[j:] = st["curL"], st["curR"]
                st["flip"] = 0
        elif st["ramp"] > 0:
            rem = st["ramp"]
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
        st["prev_mono"] = float(L[-1])

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
                  f"repeat={args.repeat}, phase={args.phase}")
            last_print = -1
            while not done.wait(timeout=0.05):
                k = st["last_k"]
                if k != last_print and 0 <= k < len(STEPS):
                    rem, ch = STEPS[k]
                    print(f">> {step_label(rem, ch, k, len(STEPS))}   "
                          f"(pass {st['passno'] + 1})")
                    last_print = k
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        if st["xruns"]:
            print(f"xruns: {st['xruns']}")


if __name__ == "__main__":
    main()
