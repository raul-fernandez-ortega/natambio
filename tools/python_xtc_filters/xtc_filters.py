#! /usr/bin/env python3
#
# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
xtc_filters.py — XTC (crosstalk cancellation) FIR filter generator, pure Python.

Python counterpart of the C tool tools/xtc_filters (which links the project's
shared lib/xtc.c → lib/dsp.c → lib/binaural_cues.c). It writes the *direct* and
*cross* filter pair as 32-bit float WAV files under ./filters/, using exactly
the same pipeline, defaults, and output-filename contract as the C tool:

    ILD target curve  →  linear-phase FIR (firwin2, Hamming)
                      →  minimum phase (oversampled homomorphic cepstrum)
                      →  XTC recursion (32 alternating direct/cross steps)

This is a faithful port of the C tool, NOT of the older ambio_filters_scipy.py:
the minimum-phase step here uses the same ×8 cepstral oversampling as lib/dsp.c
(the original scipy script transformed at length n, which aliased the cepstrum
tail and drifted the magnitude ~0.2 dB — amplified to ~5 % through the 16 chained
XTC convolutions). Output filters are therefore equivalent to the C tool's.

Usage:
    python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha \\
                           -z azimuth_deg -r sample_rate -f filter_len
    # defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096

Output (in ./filters/):
    XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
    XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav

Set DEBUG = True (or pass -d) to also dump the intermediate, peak-normalised
filters/ILD_<az>_deg.wav and filters/MP_ILD_<az>_deg.wav, mirroring the C tool's
DEBUG build.
"""

import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, firwin2

# --- Model / pipeline constants (identical to lib/xtc.c) --------------------
DEBUG = False
DB_OCT = 6.0              # LF extrapolation slope below FLIM
FLIM = 200.0             # LF/HF crossover of the ILD model
NSTEPS = 32               # XTC recursion steps
HF_SHELF_HZ = 20000.0     # HF shelf corner frequency
HF_SHELF_DB_OCT = -36.0   # shelf slope above HF_SHELF_HZ
MP_CEPSTRUM_OVERSAMPLE = 8  # cepstral transform oversampling (see lib/dsp.c)

# Input limits, mirroring lib/xtc.c so behaviour matches the C tool.
XTC_MAX_FILTER_LEN = 1 << 20     # 1048576 taps (~21 s @ 48 kHz)
XTC_MAX_SAMPLE_RATE = 768000
XTC_MAX_ILD_ALPHA = 100.0


def deg2rad(deg):
    """binaural_cues.c: deg2rad()."""
    return deg * math.pi / 180.0


def ild_log_empirical(theta, freq, alpha):
    """binaural_cues.c: ild_log_empirical(), vectorised over freq (Hz)."""
    f = np.maximum(np.asarray(freq, dtype=float), 200.0)
    a_f = alpha * 10.0 * np.log10(f / 1000.0 + 1.0)
    return a_f * np.sin(theta)


def ild_db_model(freqs_hz, theta, alpha):
    """Target ILD curve in dB — same model as ild_db_model() of lib/xtc.c.

      - HF (f > FLIM):  -ild_log_empirical(theta, f, alpha)
      - LF (f <= FLIM): -6 dB/oct anchored to the model value at FLIM
      - HF shelf: HF_SHELF_DB_OCT/oct above HF_SHELF_HZ, to smooth the step
        toward Nyquist and reduce cepstral ringing at n > filter_len/2 of the MP.

    Caller must pass only interior bins (f > 0): the endpoints are forced to
    gain 0 separately (Nyquist by the Type II constraint, DC to avoid log(0)).
    """
    f = np.asarray(freqs_hz, dtype=float)
    hf = -ild_log_empirical(theta, f, alpha)
    ild_flim = ild_log_empirical(theta, FLIM, alpha)
    lf = -DB_OCT * (math.log2(FLIM) - np.log2(f)) - ild_flim
    curve = np.where(f > FLIM, hf, lf)
    shelf = np.where(f > HF_SHELF_HZ,
                     HF_SHELF_DB_OCT * np.log2(np.maximum(f, HF_SHELF_HZ) / HF_SHELF_HZ),
                     0.0)
    return curve + shelf


def minimum_phase(x):
    """Minimum-phase reconstruction via the oversampled homomorphic cepstrum.

    Faithful port of lib/dsp.c:minimum_phase(). The cepstral transforms run on
    N = next_pow2(n) * MP_CEPSTRUM_OVERSAMPLE rather than on n: the complex
    cepstrum of a FIR has infinite support and decays only ~1/k, so an n-point
    transform aliases its tail back onto itself. Oversampling by 8 drives the
    magnitude error below 0.0002 dB. numpy's rfft/irfft carry the 1/N
    normalisation that lib/dsp.c applies by hand.
    """
    x = np.asarray(x, dtype=float)
    n = len(x)

    n_pow2 = 1
    while n_pow2 < n:
        n_pow2 <<= 1
    big_n = n_pow2 * MP_CEPSTRUM_OVERSAMPLE

    # 1) X = FFT(x zero-padded to N);  2) log|X|
    spectrum = np.fft.rfft(x, n=big_n)
    log_mag = np.log(np.abs(spectrum) + 1e-12)

    # 3) ceps = IFFT(log|X|).real
    ceps = np.fft.irfft(log_mag, n=big_n)

    # 4) fold onto the causal part: [1, 2, 2, ..., 2, 1, 0, ..., 0] (N even)
    window = np.zeros(big_n)
    window[0] = 1.0
    window[1:big_n // 2] = 2.0
    window[big_n // 2] = 1.0

    # 5) FFT(ceps * window);  6) exp();  7) IFFT, truncate to n taps
    complex_ceps = np.fft.rfft(ceps * window, n=big_n)
    min_phase_spectrum = np.exp(complex_ceps)
    return np.fft.irfft(min_phase_spectrum, n=big_n)[:n]


def get_xtc(length, attenuation, delay, ild_filter):
    """XTC recursion — port of get_xtc() in lib/xtc.c (== getRACE()).

    32 iterations alternating direct/cross taps, each convolved with the
    L2-normalised min-phase ILD filter and truncated to `length`. Taps landing
    past the end of the filter are dropped (same truncation as everything else).
    """
    direct = np.zeros(length)
    cross = np.zeros(length)
    n = (NSTEPS - 1) * delay
    cross_att = -attenuation * (NSTEPS - 1)
    dir_att = -attenuation * NSTEPS
    while n > 0:
        if n + delay < length:
            direct[n + delay] += math.pow(10.0, dir_att / 20.0)
        if n < length:
            cross[n] += -math.pow(10.0, cross_att / 20.0)
        direct = fftconvolve(direct, ild_filter)[:length]
        cross = fftconvolve(cross, ild_filter)[:length]
        dir_att += 2.0 * attenuation
        cross_att += 2.0 * attenuation
        n -= 2 * delay
    direct[0] = 1.0
    return direct, cross


def process(itd_us, ild_db, ild_alpha, azimuth_deg, sample_rate, filter_len):
    """ILD -> minimum-phase -> XTC pipeline. Port of process() in lib/xtc.c.

    Returns (direct, cross) filters. Raises ValueError on invalid input, with
    the same bounds the C tool enforces.
    """
    if not (2 <= filter_len <= XTC_MAX_FILTER_LEN):
        raise ValueError("filter_len %d outside [2, %d]" % (filter_len, XTC_MAX_FILTER_LEN))
    if not (0 < sample_rate <= XTC_MAX_SAMPLE_RATE):
        raise ValueError("sample_rate %d outside (0, %d]" % (sample_rate, XTC_MAX_SAMPLE_RATE))
    if itd_us <= 0:
        raise ValueError("itd_us must be positive (got %d)" % itd_us)
    if not (math.isfinite(ild_db) and ild_db > 0.0):
        raise ValueError("ild_db must be a positive dB step (got %g)" % ild_db)
    if not (math.isfinite(ild_alpha) and abs(ild_alpha) <= XTC_MAX_ILD_ALPHA):
        raise ValueError("ild_alpha %g outside [-%g, %g]" % (ild_alpha, XTC_MAX_ILD_ALPHA, XTC_MAX_ILD_ALPHA))

    itd_exact = itd_us * 1e-6 * sample_rate
    if itd_exact < 0.5:
        raise ValueError("ITD %d us at %d Hz gives %.3f samples, too small"
                         % (itd_us, sample_rate, itd_exact))
    itd_samples = int(round(itd_exact))

    first_tap = (NSTEPS - 1) * itd_samples
    if first_tap >= filter_len:
        sys.stderr.write(
            "xtc: warning: filter_len %d is too short for an ITD of %d samples; "
            "taps at or beyond index %d are dropped and crosstalk cancellation "
            "will be incomplete. Use filter_len > %d.\n"
            % (filter_len, itd_samples, first_tap, first_tap))

    theta = deg2rad(azimuth_deg)

    # --- Linear-phase FIR on the dense grid (firwin2, Hamming) --------------
    # nfreqs = 1 + 2^ceil(log2(filter_len)) uniform points on [0, Nyquist];
    # passing the model evaluated on exactly this grid makes firwin2's internal
    # interpolation the identity. Endpoints forced to gain 0.
    nfreqs = 1 + 2 ** int(math.ceil(math.log2(filter_len)))
    freqs_hz = np.linspace(0.0, sample_rate / 2, nfreqs)
    gains_lin = np.zeros(nfreqs)
    gains_lin[1:-1] = np.power(10.0, ild_db_model(freqs_hz[1:-1], theta, ild_alpha) / 20.0)

    h_linear = np.asarray(firwin2(filter_len, freqs_hz, gains_lin,
                                  nfreqs=nfreqs, fs=sample_rate))

    # RMS normalisation of the linear-phase filter
    rms = np.sqrt(np.mean(h_linear ** 2))
    if rms > 0.0:
        h_linear = h_linear / rms

    # Minimum phase, then RMS normalisation
    h_min = minimum_phase(h_linear)
    rms_m = np.sqrt(np.mean(h_min ** 2))
    if rms_m > 0.0:
        h_min = h_min / rms_m

    if DEBUG:
        peak_l = np.max(np.abs(h_linear))
        peak_m = np.max(np.abs(h_min))
        sf.write("filters/ILD_%d_deg.wav" % azimuth_deg,
                 h_linear / peak_l if peak_l > 0 else h_linear, sample_rate, subtype="FLOAT")
        sf.write("filters/MP_ILD_%d_deg.wav" % azimuth_deg,
                 h_min / peak_m if peak_m > 0 else h_min, sample_rate, subtype="FLOAT")

    # L2 normalisation before entering the XTC recursion
    l2 = np.linalg.norm(h_min)
    if l2 > 0.0:
        h_min = h_min / l2

    print("Calculating XTC filters for: delay --> %d samples. Attenuation --> %.2f dB. "
          "azimuth --> %d degrees" % (itd_samples, ild_db, azimuth_deg))
    print("XTC filters length: %d samples. Sample rate: %d" % (filter_len, sample_rate))

    return get_xtc(filter_len, ild_db, itd_samples, h_min)


def save_xtc_wavs(direct, cross, itd_us, ild_db, ild_alpha,
                  azimuth_deg, sample_rate):
    """Write the two final XTC filters — same filename contract as lib main.c."""
    base = ("filters/XTC_%02d_deg_ITD_%d_micsec_ILD_%.1f_dB_a_%.1f"
            % (azimuth_deg, itd_us, ild_db, ild_alpha))
    sf.write(base + "_direct.wav", direct, sample_rate, subtype="FLOAT")
    sf.write(base + "_cross.wav", cross, sample_rate, subtype="FLOAT")


USAGE = ("Usage: python3 xtc_filters.py -t ITD(microsec) -l ILD(dB positive) "
         "-a ILD_alpha(0-3) -z azimuth(degrees) -r SampleRate "
         "-f FilterLength(samples) [-d]")


def main():
    global DEBUG
    # Defaults identical to the C tool.
    itd_us = 170
    ild_db = 14.0
    ild_alpha = 2.0
    azimuth = 20
    sample_rate = 48000
    filter_len = 4096

    args = sys.argv[1:]
    if not args:
        print(USAGE)
        sys.exit(1)

    # Same flag-then-value parser as the C tool / original script.
    nextarg = ""
    for a in args:
        if a == "-t":
            nextarg = "ITD"
        elif a == "-l":
            nextarg = "ILD"
        elif a == "-a":
            nextarg = "FACTOR"
        elif a == "-z":
            nextarg = "AZIMUTH"
        elif a == "-r":
            nextarg = "SRATE"
        elif a == "-f":
            nextarg = "FILTERLEN"
        elif a == "-d":
            DEBUG = True
        elif a in ("-h", "-u"):
            print(USAGE)
            sys.exit(0)
        elif a[0] != "-":
            if nextarg == "ITD":
                itd_us = int(a)
            elif nextarg == "ILD":
                ild_db = float(a)
            elif nextarg == "AZIMUTH":
                azimuth = int(a)
            elif nextarg == "FACTOR":
                ild_alpha = float(a)
            elif nextarg == "SRATE":
                sample_rate = int(a)
            elif nextarg == "FILTERLEN":
                filter_len = int(a)

    os.makedirs("filters", exist_ok=True)

    try:
        direct, cross = process(itd_us, ild_db, ild_alpha, azimuth,
                                sample_rate, filter_len)
    except ValueError as e:
        sys.stderr.write("xtc: %s\n" % e)
        sys.exit(2)

    save_xtc_wavs(direct, cross, itd_us, ild_db, ild_alpha, azimuth, sample_rate)


if __name__ == "__main__":
    main()
