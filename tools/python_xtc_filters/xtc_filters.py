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
    python3 xtc_filters.py -c config.toml
    python3 xtc_filters.py -t ITD_us -l ILD_dB -a ILD_alpha \\
                           -z azimuth_deg -r sample_rate -f filter_len
    # defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096

The ITD is rounded to whole samples, exactly as the C tool does. Pass -F to
run the XTC recursion in the frequency domain at the exact, unrounded ITD
instead (get_xtc_freq): a shift becomes a linear-phase factor, which removes
the rounding entirely. At 48 kHz the default 170 us is 8.16 samples, and
rounding it to 8 caps crosstalk suppression near 10 kHz — see
compare_frac_delay.py, which measures both against the modelled plant. -F is
off by default so the tool keeps matching the C tool bit for bit; the same
switch is available as the top-level frac_delay / model_delay TOML keys, read
by the C tools as well.

Parameters can come from a TOML file (-c) or from flags, and the two combine:
the file is read where it appears in the command line, so flags after it win.
The TOML files are the very same ones the C tools read — see
../xtc_filters/xtc_sym_default.toml and ../xtc_filters/xtc_sym_wide.toml.

Output (in ./filters/, or output.directory):
    XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_direct.wav
    XTC_<az>_deg_ITD_<itd>_micsec_ILD_<ild>_dB_a_<alpha>_cross.wav
    # or <output.prefix>_direct.wav / <output.prefix>_cross.wav when a prefix
    # is configured

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

import xtc_conf

# --- Model / pipeline constants (identical to lib/xtc.c) --------------------
DEBUG = False
# Where the DEBUG dumps go; main() points it at output.directory.
OUTPUT_DIR = "filters"
DB_OCT = 6.0              # LF extrapolation slope below FLIM
FLIM = 200.0             # LF/HF crossover of the ILD model
NSTEPS = 32               # XTC recursion steps
HF_SHELF_HZ = 20000.0     # HF shelf corner frequency
HF_SHELF_DB_OCT = -36.0   # shelf slope above HF_SHELF_HZ
MP_CEPSTRUM_OVERSAMPLE = 8  # cepstral transform oversampling (see lib/dsp.c)
# Bulk delay added to both filters by the fractional-ITD path, so the
# two-sided response of a fractional shift is not clipped at n = 0. 64 taps
# put the discarded pre-ring at -52 dB of the cross filter, ~20 dB below the
# recursion's own residual; the cost is 1.3 ms of latency at 48 kHz.
DEFAULT_MODEL_DELAY = 64

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


def build_ild_filter(ild_alpha, azimuth_deg, sample_rate, filter_len):
    """ILD target curve -> linear-phase FIR -> minimum phase -> L2 norm.

    The head of process(), lifted out unchanged so that tools which need the
    very same ILD filter (compare_frac_delay.py) cannot drift from it. Mirrors
    build_ild_filter() of lib/xtc_asym.c. Returns the L2-normalised min-phase
    filter that the XTC recursion consumes.
    """
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
        sf.write(os.path.join(OUTPUT_DIR, "ILD_%d_deg.wav" % azimuth_deg),
                 h_linear / peak_l if peak_l > 0 else h_linear, sample_rate, subtype="FLOAT")
        sf.write(os.path.join(OUTPUT_DIR, "MP_ILD_%d_deg.wav" % azimuth_deg),
                 h_min / peak_m if peak_m > 0 else h_min, sample_rate, subtype="FLOAT")

    # L2 normalisation before entering the XTC recursion
    l2 = np.linalg.norm(h_min)
    if l2 > 0.0:
        h_min = h_min / l2
    return h_min


def _delay_spectrum(freqs, samples):
    """Half-spectrum of a pure delay of `samples` samples (may be fractional).

    exp(-j*2*pi*f*samples) on the rfft grid. This is the exact band-limited
    delay operator on a periodic grid: no interpolation kernel, no rounding,
    and no accuracy/length trade-off. The Nyquist bin of a real, even-length
    signal must itself be real, which a fractional shift violates; forcing it
    to its real part (cos(pi*samples)) is the standard resolution and is
    immaterial here because the ILD shelf leaves |A| ~ 0.02 at Nyquist.
    """
    z = np.exp(-2j * np.pi * np.asarray(freqs) * float(samples))
    z[-1] = z[-1].real
    return z


def get_xtc_freq(length, attenuation, delay, ild_filter, model_delay=0, nfft=None):
    """XTC recursion in the frequency domain — exact for a fractional ITD.

    Spectral counterpart of get_xtc(), iteration for iteration: the same 16
    passes, the same tap positions and gains, the same Horner ordering (insert
    the tap, then filter the whole accumulator with the ILD). The only change
    is how a tap is placed. get_xtc() writes it at an integer array index, so
    the ITD must first be rounded; here it is a linear-phase factor evaluated
    at the exact, fractional ITD.

    `delay` is the ITD in samples and is a float. With an integer `delay` this
    function reproduces get_xtc() to numerical precision.

    Why the rounding matters: an ITD error of dt leaves a residual
    2*sin(pi*f*dt) relative to the cancelling signal, so at 48 kHz the default
    170 us (8.16 samples -> 8) caps crosstalk suppression at about -14 dB at
    10 kHz, where the exact ITD is limited only by the series truncation.

    `model_delay` is a bulk integer delay applied to both outputs. A fractional
    shift has a two-sided impulse response, so the first cross tap (at one ITD,
    ~8 samples in) has energy at negative time; without a bulk delay that
    energy wraps past the end of the FFT frame and is discarded by the
    truncation to `length`. Delaying both filters equally is harmless: XTC only
    depends on the delay *between* them.

    The frame `nfft` is sized to hold the whole linear convolution, so nothing
    wraps into [0, length). Note that get_xtc()'s truncation to `length` after
    every iteration is not an approximation being dropped here: every later
    operation is a causal convolution, so samples at n >= length can never fold
    back onto n < length. Truncating each pass and truncating once at the end
    give the same first `length` taps.
    """
    delay = float(delay)
    if nfft is None:
        span = (int(math.ceil(NSTEPS * delay))
                + (NSTEPS // 2) * (len(ild_filter) - 1)
                + int(model_delay) + length + 1)
        nfft = 1 << (span - 1).bit_length()

    freqs = np.fft.rfftfreq(nfft)                 # cycles per sample
    ild_spec = np.fft.rfft(ild_filter, n=nfft)

    direct = np.zeros(freqs.size, dtype=complex)
    cross = np.zeros(freqs.size, dtype=complex)

    k = NSTEPS - 1
    cross_att = -attenuation * (NSTEPS - 1)
    dir_att = -attenuation * NSTEPS
    while k > 0:
        direct += math.pow(10.0, dir_att / 20.0) * _delay_spectrum(freqs, (k + 1) * delay)
        cross -= math.pow(10.0, cross_att / 20.0) * _delay_spectrum(freqs, k * delay)
        direct *= ild_spec
        cross *= ild_spec
        dir_att += 2.0 * attenuation
        cross_att += 2.0 * attenuation
        k -= 2
    direct += 1.0                                 # main delta; get_xtc() writes direct[0]

    if model_delay:
        ramp = _delay_spectrum(freqs, int(model_delay))
        direct *= ramp
        cross *= ramp

    return (np.fft.irfft(direct, n=nfft)[:length],
            np.fft.irfft(cross, n=nfft)[:length])


def process(itd_us, ild_db, ild_alpha, azimuth_deg, sample_rate, filter_len,
            frac_delay=False, model_delay=DEFAULT_MODEL_DELAY):
    """ILD -> minimum-phase -> XTC pipeline. Port of process() in lib/xtc.c.

    Returns (direct, cross) filters. Raises ValueError on invalid input, with
    the same bounds the C tool enforces.

    With frac_delay the XTC recursion runs in the frequency domain at the
    exact, unrounded ITD (get_xtc_freq); everything upstream is untouched.
    Default stays False so the tool keeps producing filters identical to the
    C tool's.
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

    h_min = build_ild_filter(ild_alpha, azimuth_deg, sample_rate, filter_len)

    if frac_delay:
        print("Calculating XTC filters for: delay --> %.4f samples (fractional, "
              "frequency domain; %d-sample model delay). Attenuation --> %.2f dB. "
              "azimuth --> %d degrees"
              % (itd_exact, model_delay, ild_db, azimuth_deg))
        print("XTC filters length: %d samples. Sample rate: %d" % (filter_len, sample_rate))
        return get_xtc_freq(filter_len, ild_db, itd_exact, h_min, model_delay=model_delay)

    print("Calculating XTC filters for: delay --> %d samples. Attenuation --> %.2f dB. "
          "azimuth --> %d degrees" % (itd_samples, ild_db, azimuth_deg))
    print("XTC filters length: %d samples. Sample rate: %d" % (filter_len, sample_rate))

    return get_xtc(filter_len, ild_db, itd_samples, h_min)


def save_xtc_wavs(cfg, direct, cross):
    """Write the two final XTC filters.

    With no output.prefix the historical descriptive filename contract is kept
    verbatim, since it is what the surrounding scripts and the documentation
    expect. A prefix replaces the whole descriptive part, which is the sensible
    choice once the parameters live in a TOML file that is itself the record of
    the design.
    """
    side = cfg["xtc"]
    if cfg["prefix"]:
        stem = cfg["prefix"]
    else:
        stem = ("XTC_%02d_deg_ITD_%d_micsec_ILD_%.1f_dB_a_%.1f"
                % (side["azimuth_deg"], side["itd_us"],
                   side["ild_db"], side["ild_alpha"]))
        if cfg.get("frac_delay"):
            stem += "_frac"
    base = os.path.join(cfg["directory"], stem)
    sf.write(base + "_direct.wav", direct, cfg["sample_rate"], subtype="FLOAT")
    sf.write(base + "_cross.wav", cross, cfg["sample_rate"], subtype="FLOAT")


USAGE = ("Usage: python3 xtc_filters.py [-c config.toml] [-t ITD(microsec)] "
         "[-l ILD(dB positive)] [-a ILD_alpha(0-3)] [-z azimuth(degrees)] "
         "[-r SampleRate] [-f FilterLength(samples)] [-F] [-M ModelDelay] [-d]\n"
         "  -c FILE  read parameters from a TOML file; flags given after it "
         "override the file.\n"
         "  -F       fractional ITD: run the XTC recursion in the frequency "
         "domain at the exact,\n"
         "           unrounded ITD instead of rounding it to whole samples. "
         "Appends _frac to the\n"
         "           output filenames. See compare_frac_delay.py.\n"
         "  -M N     bulk model delay in samples for -F (default %d)."
         % DEFAULT_MODEL_DELAY)


def default_config():
    """Defaults identical to the C tool."""
    return {
        "sample_rate": 48000,
        "filter_len": 4096,
        "xtc": {"itd_us": 170, "ild_db": 14.0, "ild_alpha": 2.0, "azimuth_deg": 20},
        "directory": "filters",
        "prefix": "",
        # Also readable from TOML as the top-level frac_delay / model_delay
        # keys, which the C tools understand too; -F / -M override the file the
        # same way every other flag does.
        "frac_delay": False,
        "model_delay": DEFAULT_MODEL_DELAY,
    }


def main():
    global DEBUG, OUTPUT_DIR
    cfg = default_config()

    args = sys.argv[1:]
    if not args:
        print(USAGE)
        sys.exit(1)

    # Same flag-then-value parser as the C tool / original script, with -c added.
    nextarg = ""
    for a in args:
        if a == "-c":
            nextarg = "CONFIG"
        elif a == "-t":
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
        elif a == "-F":
            cfg["frac_delay"] = True
        elif a == "-M":
            nextarg = "MODELDELAY"
        elif a == "-d":
            DEBUG = True
        elif a in ("-h", "-u"):
            print(USAGE)
            sys.exit(0)
        elif a[0] != "-":
            if nextarg == "ITD":
                cfg["xtc"]["itd_us"] = int(a)
            elif nextarg == "ILD":
                cfg["xtc"]["ild_db"] = float(a)
            elif nextarg == "AZIMUTH":
                cfg["xtc"]["azimuth_deg"] = int(a)
            elif nextarg == "FACTOR":
                cfg["xtc"]["ild_alpha"] = float(a)
            elif nextarg == "SRATE":
                cfg["sample_rate"] = int(a)
            elif nextarg == "FILTERLEN":
                cfg["filter_len"] = int(a)
            elif nextarg == "MODELDELAY":
                cfg["model_delay"] = int(a)
            elif nextarg == "CONFIG":
                try:
                    xtc_conf.load_sym(a, cfg)
                except xtc_conf.ConfError as e:
                    sys.stderr.write("xtc: %s: %s\n" % (a, e))
                    sys.exit(4)

    OUTPUT_DIR = cfg["directory"]
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    side = cfg["xtc"]
    try:
        direct, cross = process(side["itd_us"], side["ild_db"], side["ild_alpha"],
                                side["azimuth_deg"], cfg["sample_rate"],
                                cfg["filter_len"], frac_delay=cfg["frac_delay"],
                                model_delay=cfg["model_delay"])
    except ValueError as e:
        sys.stderr.write("xtc: %s\n" % e)
        sys.exit(2)

    save_xtc_wavs(cfg, direct, cross)


if __name__ == "__main__":
    main()
