#! /usr/bin/env python3
#
# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
xtc_filters_asym.py — asymmetric XTC FIR filter generator, pure Python.

Python counterpart of the C tool tools/xtc_filters (natambio-xtc-filters-asym,
which links lib/xtc_asym.c), and the asymmetric sibling of xtc_filters.py. It
covers layouts where the two crossed acoustic paths are not the same, whether
because the speakers sit at different angles or because the room is acoustically
asymmetric under an otherwise symmetric placement. The model is described in
docs/xtc/xtc_no_simetrico_es.md (English: xtc_no_simetrico_en.md).

Three filters come out, not four: the two direct filters of the model are
identical, because they depend only on the round-trip operator P = G_l * G_r,
which is symmetric under exchange of channels. What differs between channels is
the cross filter.

    python3 xtc_filters_asym.py -c config.toml

Output (in ./filters/, or output.directory):
    <prefix>_direct.wav        shared by both channels
    <prefix>_cross_left.wav    feeds the LEFT speaker
    <prefix>_cross_right.wav   feeds the RIGHT speaker

Note which side feeds which speaker: the cross filter feeding the LEFT speaker
is built from the RIGHT side's parameters, because it cancels the right
speaker's leakage into the left ear, and the only direct path reaching that ear
is the left speaker's. Everything in that branch is "right" — the ITD, the ILD
and the spectrum — except the speaker that radiates it.

The balance b is NOT applied here. This produces M^-1; the level trim between
channels belongs downstream as a routing gain, exactly as with the <xtc_asym>
block of natambio. Leaving it unadjusted caps cancellation at about
20*log10|1-b| dB, so it is not optional — it is simply not part of these
coefficients. The balance section of the technical note gives the procedure.

The DSP primitives (ILD model, minimum phase, constants) are imported from
xtc_filters.py rather than copied. The C side duplicates them in xtc_asym.c
instead, but only because lib/xtc.c is mirrored by third-party ports and has to
stay byte-stable; no such constraint applies here.
"""

import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, firwin2

import xtc_conf
from xtc_filters import (
    XTC_MAX_FILTER_LEN,
    XTC_MAX_ILD_ALPHA,
    XTC_MAX_SAMPLE_RATE,
    NSTEPS,
    deg2rad,
    ild_db_model,
    minimum_phase,
)

# Rungs of the ladder. xtc.c walks NSTEPS in half-steps, i.e. NSTEPS/2 round
# trips; the asymmetric recursion is written directly in round trips, so both
# produce the same number of terms.
NRUNGS = NSTEPS // 2


def build_ild_filter(theta, alpha, sample_rate, filter_len):
    """One G of the model: ILD magnitude -> minimum phase -> normalisation.

    Port of build_ild_filter() in lib/xtc_asym.c. Same sequence, same
    normalisations and same order as process() in xtc_filters.py, so that
    identical parameters yield an identical filter.

    theta is in radians; alpha is the model scale factor (or, when theta = pi/2,
    the alpha*sin(theta) product directly).
    """
    nfreqs = 1 + 2 ** int(math.ceil(math.log2(filter_len)))
    freqs_hz = np.linspace(0.0, sample_rate / 2, nfreqs)
    gains_lin = np.zeros(nfreqs)
    gains_lin[1:-1] = np.power(10.0, ild_db_model(freqs_hz[1:-1], theta, alpha) / 20.0)

    h_linear = np.asarray(firwin2(filter_len, freqs_hz, gains_lin,
                                  nfreqs=nfreqs, fs=sample_rate))

    rms = np.sqrt(np.mean(h_linear ** 2))
    if rms > 0.0:
        h_linear = h_linear / rms

    h_min = minimum_phase(h_linear)
    rms_m = np.sqrt(np.mean(h_min ** 2))
    if rms_m > 0.0:
        h_min = h_min / rms_m

    # L2 normalisation before entering the recursion: the filter must carry
    # spectral shape only, never level. The level lives in the per-rung dB terms
    # of get_xtc_asym().
    l2 = np.linalg.norm(h_min)
    if l2 > 0.0:
        h_min = h_min / l2
    return h_min


def get_xtc_asym(length, att_l, att_r, delay_l, delay_r, ild_mean, ild_l, ild_r):
    """Asymmetric XTC recursion — port of get_xtc_asym() in lib/xtc_asym.c.

    Returns (direct, cross_left, cross_right). Produces M^-1: no balance factor.

    The ladder is built from the top rung down, exactly as in get_xtc(): a tap
    added at iteration k is convolved once per remaining iteration, so it ends up
    carrying k applications of the round-trip ILD filter. That is the convention
    of the symmetric implementation — the ILD *spectrum* is applied once per
    round trip while the delay and the broadband ILD follow the full
    (2i-1)/(2i) law — and it is reproduced here so that equal parameters on both
    sides give the same filters as the symmetric generator.

    Rung i of the direct filter sits at i*T with (a_l*a_r)^i, and rung i of the
    cross filter feeding the left speaker sits at (i-1)*T + delay_r with
    a_r*(a_l*a_r)^(i-1) — it cancels the RIGHT speaker's crosstalk, hence the
    right side's delay, level and spectrum.
    """
    if length < 2 or length > XTC_MAX_FILTER_LEN:
        raise ValueError("filter_len %d outside [2, %d]" % (length, XTC_MAX_FILTER_LEN))
    if delay_l <= 0 or delay_r <= 0:
        raise ValueError("ITDs must be positive (got %d, %d samples)" % (delay_l, delay_r))
    # Both attenuations must be positive dB steps. This is the convergence
    # condition |G_l * G_r| < 1: with att_l, att_r > 0 every rung is strictly
    # below the previous one. Note the condition is on the *product*, so the two
    # sides need not be equally attenuated.
    if not (math.isfinite(att_l) and att_l > 0.0):
        raise ValueError("left ild_db must be a positive dB step (got %g)" % att_l)
    if not (math.isfinite(att_r) and att_r > 0.0):
        raise ValueError("right ild_db must be a positive dB step (got %g)" % att_r)

    direct = np.zeros(length)
    cross_l = np.zeros(length)
    cross_r = np.zeros(length)

    period = delay_l + delay_r
    att_step = att_l + att_r

    for k in range(NRUNGS, 0, -1):
        # Taps landing past the end of the filter are dropped rather than
        # written; same truncation the convolutions apply to everything else.
        dir_idx = k * period
        if dir_idx < length:
            direct[dir_idx] += math.pow(10.0, -k * att_step / 20.0)

        # Cross rung i = k+1: added one iteration after the direct rung of the
        # same index so that it accumulates i-1 round-trip convolutions, the
        # remaining one being the per-side filter applied after the loop.
        if k < NRUNGS:
            l_idx = k * period + delay_r
            r_idx = k * period + delay_l
            if l_idx < length:
                cross_l[l_idx] += -math.pow(10.0, -(att_r + k * att_step) / 20.0)
            if r_idx < length:
                cross_r[r_idx] += -math.pow(10.0, -(att_l + k * att_step) / 20.0)

        direct = fftconvolve(direct, ild_mean)[:length]

        # Skipped on the first pass, where both cross accumulators are still all
        # zeros and the convolution would be pure work.
        if k < NRUNGS:
            cross_l = fftconvolve(cross_l, ild_mean)[:length]
            cross_r = fftconvolve(cross_r, ild_mean)[:length]

    # Rung i = 1 of each cross filter: the first cancellation, which has not made
    # any round trip yet and therefore carries no round-trip filter.
    if delay_r < length:
        cross_l[delay_r] += -math.pow(10.0, -att_r / 20.0)
    if delay_l < length:
        cross_r[delay_l] += -math.pow(10.0, -att_l / 20.0)

    # The single per-side application: every cross rung crosses its own side's
    # head shadow exactly once, whatever its order. This is what keeps the two
    # cross filters spectrally distinct in an asymmetric layout.
    cross_l = fftconvolve(cross_l, ild_r)[:length]
    cross_r = fftconvolve(cross_r, ild_l)[:length]

    # The leading delta: each channel's signal is delivered intact and the ladder
    # is added on top.
    direct[0] = 1.0
    return direct, cross_l, cross_r


def check_side(side, tag, sample_rate):
    """Validates one side and converts its ITD to samples.

    Port of check_side() in lib/xtc_asym.c.
    """
    if side["itd_us"] <= 0:
        raise ValueError("[%s] itd_us must be positive (got %d)" % (tag, side["itd_us"]))
    if not (math.isfinite(side["ild_db"]) and side["ild_db"] > 0.0):
        raise ValueError("[%s] ild_db must be a positive dB step (got %g)"
                         % (tag, side["ild_db"]))
    if not (math.isfinite(side["ild_alpha"]) and abs(side["ild_alpha"]) <= XTC_MAX_ILD_ALPHA):
        raise ValueError("[%s] ild_alpha %g outside [-%g, %g]"
                         % (tag, side["ild_alpha"], XTC_MAX_ILD_ALPHA, XTC_MAX_ILD_ALPHA))

    itd_exact = side["itd_us"] * 1e-6 * sample_rate
    if itd_exact < 0.5:
        raise ValueError("[%s] ITD %d us at %d Hz gives %.3f samples, too small"
                         % (tag, side["itd_us"], sample_rate, itd_exact))
    return int(round(itd_exact))


def process_asym(left, right, sample_rate, filter_len):
    """ILD -> minimum-phase -> asymmetric XTC pipeline.

    Port of process_asym() in lib/xtc_asym.c. `left` and `right` are dicts with
    the four side keys. Returns (direct, cross_left, cross_right).
    """
    if not (2 <= filter_len <= XTC_MAX_FILTER_LEN):
        raise ValueError("filter_len %d outside [2, %d]" % (filter_len, XTC_MAX_FILTER_LEN))
    if not (0 < sample_rate <= XTC_MAX_SAMPLE_RATE):
        raise ValueError("sample_rate %d outside (0, %d]" % (sample_rate, XTC_MAX_SAMPLE_RATE))

    itd_l = check_side(left, "left", sample_rate)
    itd_r = check_side(right, "right", sample_rate)

    # The ladder's top rung sits at NRUNGS*(itd_l+itd_r). If that is already past
    # the end of the filter those taps are dropped and cancellation comes out
    # incomplete. Design still proceeds — the result is a valid, weaker filter —
    # but the user gets told. Unlike the symmetric case, what bounds the ladder
    # is the SUM of both ITDs, so a layout can stay within budget by keeping the
    # mean ITD even when the two sides differ widely.
    top_tap = NRUNGS * (itd_l + itd_r)
    if top_tap >= filter_len:
        sys.stderr.write(
            "xtc_asym: warning: filter_len %d is too short for ITDs of %d + %d "
            "samples; taps at or beyond index %d are dropped and crosstalk "
            "cancellation will be incomplete. Use filter_len > %d.\n"
            % (filter_len, itd_l, itd_r, top_tap, top_tap))

    theta_l = deg2rad(left["azimuth_deg"])
    theta_r = deg2rad(right["azimuth_deg"])

    # Round-trip filter. Applying the ILD spectrum once per round trip leaves
    # room for a single filter per rung, and a round trip crosses one head shadow
    # on each side; its log-magnitude is therefore the mean of both sides'. Since
    # the model is linear in alpha*sin(theta), that mean is exactly the model
    # evaluated at theta = pi/2 with alpha = mean of the two alpha*sin(theta)
    # products — no averaging of responses needed. With both sides equal it
    # collapses to the side filter, which is what makes the symmetric case reduce
    # to the symmetric generator.
    kappa_mean = 0.5 * (left["ild_alpha"] * math.sin(theta_l) +
                        right["ild_alpha"] * math.sin(theta_r))

    ild_l = build_ild_filter(theta_l, left["ild_alpha"], sample_rate, filter_len)
    ild_r = build_ild_filter(theta_r, right["ild_alpha"], sample_rate, filter_len)
    ild_mean = build_ild_filter(math.pi / 2.0, kappa_mean, sample_rate, filter_len)

    print("Calculating asymmetric XTC filters:")
    print("\tleft : delay --> %d samples. Attenuation --> %.2f dB. "
          "azimuth --> %d degrees. alpha --> %.2f"
          % (itd_l, left["ild_db"], left["azimuth_deg"], left["ild_alpha"]))
    print("\tright: delay --> %d samples. Attenuation --> %.2f dB. "
          "azimuth --> %d degrees. alpha --> %.2f"
          % (itd_r, right["ild_db"], right["azimuth_deg"], right["ild_alpha"]))
    print("XTC filters length: %d samples. Sample rate: %d" % (filter_len, sample_rate))

    return get_xtc_asym(filter_len, left["ild_db"], right["ild_db"],
                        itd_l, itd_r, ild_mean, ild_l, ild_r)


def save_asym_wavs(cfg, direct, cross_l, cross_r):
    """Write the three filters as <prefix>_{direct,cross_left,cross_right}.wav.

    Unlike the symmetric tool there is no descriptive default filename encoding
    the parameters: with eight of them the name would be unreadable, and the TOML
    file is already the record of the design.
    """
    prefix = cfg["prefix"] or "XTC_asym"
    base = os.path.join(cfg["directory"], prefix)
    rate = cfg["sample_rate"]
    sf.write(base + "_direct.wav", direct, rate, subtype="FLOAT")
    sf.write(base + "_cross_left.wav", cross_l, rate, subtype="FLOAT")
    sf.write(base + "_cross_right.wav", cross_r, rate, subtype="FLOAT")


USAGE = (
    "Usage: python3 xtc_filters_asym.py -c config.toml\n"
    "\n"
    "Generates the three asymmetric XTC filters (one direct, shared by both\n"
    "channels, and one cross filter per channel) from a TOML description of the\n"
    "two sides. See ../xtc_filters/xtc_asym_geometry.toml and\n"
    "../xtc_filters/xtc_asym_room.toml for annotated examples.\n"
    "\n"
    "The balance between channels is not baked into these coefficients; it is\n"
    "applied as a routing gain. See the balance section of\n"
    "docs/xtc/xtc_no_simetrico_es.md."
)


def default_config():
    return {
        "sample_rate": 48000,
        "filter_len": 4096,
        "left": {},
        "right": {},
        "directory": "filters",
        "prefix": "",
    }


def main():
    args = sys.argv[1:]
    if not args:
        print(USAGE)
        sys.exit(1)

    config_path = None
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-c":
            if i + 1 >= len(args):
                sys.stderr.write("xtc_asym: -c needs a file argument\n")
                sys.exit(1)
            config_path = args[i + 1]
            i += 2
            continue
        if a in ("-h", "-u"):
            print(USAGE)
            sys.exit(0)
        sys.stderr.write("xtc_asym: unexpected argument '%s'\n" % a)
        print(USAGE)
        sys.exit(1)

    if config_path is None:
        print(USAGE)
        sys.exit(1)

    cfg = default_config()
    try:
        xtc_conf.load_asym(config_path, cfg)
    except xtc_conf.ConfError as e:
        sys.stderr.write("xtc_asym: %s: %s\n" % (config_path, e))
        sys.exit(4)

    os.makedirs(cfg["directory"], exist_ok=True)

    try:
        direct, cross_l, cross_r = process_asym(cfg["left"], cfg["right"],
                                                cfg["sample_rate"], cfg["filter_len"])
    except ValueError as e:
        sys.stderr.write("xtc_asym: %s\n" % e)
        sys.exit(2)

    save_asym_wavs(cfg, direct, cross_l, cross_r)


if __name__ == "__main__":
    main()
