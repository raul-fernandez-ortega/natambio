# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
pca4drc.py — PCA of impulse responses for DRC, standalone Python version.

NO graphical analysis. It only performs the processing
and saves the resulting PCA components in WAV format.

Algorithm:

  1. Reads the measured impulse responses from an input directory with
     soundfile, matched by name (`--impulse-glob`) so that DRC's own outputs
     left in the same directory are not read back in as measurements.
  2. Locates the peak (absolute maximum) of each impulse.
  3. Rewrites each impulse as a signal of length `output_len` centred on its
     peak, optionally refines that centring by covariance maximisation (see
     `--align` below), and applies a Blackman window (same as pca.py).
  4. Subtracts the mean of each impulse and computes the PCA transform:
     covariance matrix between impulses, eigenvalues/eigenvectors (sorted
     descending) and projection of the impulses onto the eigenvectors. Component
     0 is always the principal one.
  5. Saves each PCA component (length `output_len`) as a WAV, normalised by the
     peak of the principal component and with corrected polarity, numbered by
     their algorithm order (PCA_0.wav, PCA_1.wav, ...), in a `pca4drc/`
     subdirectory inside the input directory (created if it does not exist).

Alignment (`--align`):

  `peak` (the default) centres every impulse on its own sample peak: a single
  sample decides, and the result is quantised to the sample grid. Half a
  sample at 48 kHz is 10 us, which is 60 degrees of phase at 16 kHz, so what
  is left after centring is not negligible for the covariance.

  `xcorr` keeps that centring as its starting point and refines it to a
  fraction of a sample by maximising the covariance between every pair of
  impulses. It uses the whole waveform instead of one sample, so a strong
  early reflection close to the direct sound cannot send it to the wrong
  arrival. On a real four-way set it raised the variance explained by the
  principal component from 41-61 % to 50-67 %, and it more than halved the
  second component: part of what looked like a second acoustic mode was
  nothing but sub-sample misalignment. It does NOT remove the difference
  between the principal component and the power average of the set: position
  to position the room differs by modal nulls and reflection patterns, not by
  delays, so no per-impulse delay can make a coherent sum behave like a power
  average.

Level reference:

  The principal component is defined up to an arbitrary scale factor, and that
  factor is DIFFERENT for every way: it depends on the covariance structure of
  the way's own impulse set (the eigenvector weights), on the peak
  normalisation of step 5, and on the mean subtraction and windowing of steps
  3-4. DRC then normalises its own stages too, so the filter it produces
  carries no memory of how loud that way actually plays. Levels measured on a
  set of ways therefore stop being comparable once they go through the
  PCA + DRC chain: on a real four-way set the arbitrary factor varied 5 dB.

  The `--level-normalize` mode restores the reference from the measurements
  themselves. Each filter is scaled so that the in-band power its way delivers
  AFTER correction equals the one it delivered when measured, both taken as
  power averages over the measurement positions -- the arithmetic mean of the
  per-position powers, which is the energy the way puts into the listening
  region, rather than the mean of the per-position differences in dB, which
  weights every position alike however little it contributes. The two disagree
  by over a decibel on modal material.

  Nothing is assumed about the shape of the filter, which is what makes this
  exact. Normalising a filter against white noise instead assumes it is flat
  inside the band; a correction filter is roughly the inverse of the response
  it corrects, so the two are anti-correlated there and the residual error
  depends on how much each way's response varies inside the band. On simulated
  four-way material that approximation left up to 1.8 dB of mismatch between
  ways.

  The reference is the balance the system had when it was measured, and is
  only as good as that balance was. The measurements have to sit beside their
  filter (`--impulse-glob`), and the reference band has to lie inside the
  passband of every way compared.

Usage:
    python pca4drc.py <impulse_directory> <output_len> [--align peak|xcorr]
    python pca4drc.py --level-normalize <filter.wav> [<filter.wav> ...]

Requires only numpy and soundfile (cross-platform): runs on GNU/Linux and
MS Windows without modification. This directory (tools/python_pca4drc) is a
standalone Python project: it shares no code with the rest of the repository.

Author : Raul Fernandez Ortega
"""

import os
import sys
import glob
import argparse
import numpy as np
import soundfile as sf


def str2bool(value):
    """Parse an inline 'true'/'false' argument into a bool."""
    if isinstance(value, bool):
        return value
    if str(value).lower() in ("true", "1", "yes", "y", "t", "on"):
        return True
    if str(value).lower() in ("false", "0", "no", "n", "f", "off"):
        return False
    raise argparse.ArgumentTypeError(f"Expected true/false, got '{value}'")


def read_impulse(path):
    """Read a WAV impulse with soundfile, returning (signal, samplerate).

    If the file has more than one channel, only the first channel is used
    (measured impulse responses are normally mono).
    """
    data, samplerate = sf.read(path, dtype="float64")
    if data.ndim > 1:
        print(f"\t\tWARNING: {os.path.basename(path)} has {data.shape[1]} "
              f"channels; using channel 0.")
        data = data[:, 0]
    return data, samplerate


def center_on_peak(signal, peak_pos, output_len):
    """Return a length-`output_len` copy of `signal` centred on `peak_pos`.

    The sample at `peak_pos` is placed at output_len//2; the rest is filled from
    the original signal where it overlaps, and zero-padded elsewhere.
    """
    out = np.zeros(output_len, dtype=np.float64)
    # Peak lands at (output_len-1)//2: the middle sample for odd lengths, the
    # lower-middle for even lengths. This matches pyDRC's ReadSignal centring
    # convention, so the output lines up sample-accurately with that pipeline.
    center = (output_len - 1) // 2
    # Destination index d maps to source index s = d - center + peak_pos.
    # Compute the overlapping ranges in source and destination coordinates.
    src_start = peak_pos - center
    src_end = src_start + output_len            # exclusive
    d_start = max(0, -src_start)                # left zero-pad in destination
    s_start = max(0, src_start)
    s_end = min(len(signal), src_end)
    n = s_end - s_start
    if n > 0:
        out[d_start:d_start + n] = signal[s_start:s_end]
    return out


# Alignment by covariance maximisation (--align xcorr). The correlation window
# only has to cover the direct sound and the first reflections: on real
# measurements 20 ms and 200 ms give the same lags to the second decimal. The
# lag bound keeps a pathological correlation from moving an impulse to a
# different arrival; the peak centring is already the starting point, so the
# residual lags are a fraction of a sample.
XCORR_HALF_WINDOW_MS = 20.0
XCORR_MAX_LAG_MS = 2.0


def pairwise_lags(segments, max_lag):
    """Consistent per-signal lags from all-pairs covariance maximisation.

    For every pair (i, j) the lag maximising their cross-correlation is
    located, with sub-sample refinement by parabolic interpolation of the
    correlation peak. The antisymmetric matrix of pairwise lags is then reduced
    to one lag per signal by least squares with a zero-sum constraint, whose
    closed form is the row mean. Using all pairs instead of a single reference
    signal means no individual measurement can drag the whole set.

    Returns (lags, hit_bound, quality): the lags in samples, whether any
    pairwise estimate landed on the +/- max_lag bound (a sign that the
    correlation found a different arrival, not a refinement), and the
    normalised peak correlation of every pair, which says how much the
    impulses of the set look like each other at all. Note that the quality
    figure is a description of the set, not a test: on real measurements it
    runs around 0.7 between close positions and drops to 0.4 between distant
    ones, a range a broken set can sit inside as well."""
    count = len(segments)
    fft_len = 1 << int(np.ceil(np.log2(2 * segments.shape[1])))
    spectra = np.fft.rfft(segments, fft_len, axis=1)
    norms = np.sqrt(np.sum(segments ** 2, axis=1))
    norms[norms == 0.0] = 1.0
    lag_matrix = np.zeros((count, count))
    quality = []
    hit_bound = False
    for i in range(count):
        for j in range(i + 1, count):
            correlation = np.fft.irfft(spectra[i] * np.conj(spectra[j]), fft_len)
            # Reorder into lags -max_lag .. +max_lag (the correlation comes out
            # wrapped, negative lags at the end).
            window = np.concatenate((correlation[-max_lag:], correlation[:max_lag + 1]))
            k = int(np.argmax(window))
            lag = float(k - max_lag)
            if k == 0 or k == len(window) - 1:
                hit_bound = True
            else:
                curvature = window[k - 1] - 2.0 * window[k] + window[k + 1]
                if curvature != 0.0:
                    lag -= 0.5 * (window[k + 1] - window[k - 1]) / curvature
            lag_matrix[i, j] = lag
            lag_matrix[j, i] = -lag
            quality.append(window[k] / (norms[i] * norms[j]))
    lags = lag_matrix.mean(axis=1)
    return lags - lags.mean(), hit_bound, np.array(quality)


def fractional_shift(signal, shift):
    """Delay `signal` by `shift` samples (a real number) with a linear phase.

    Exact for a band-limited signal, and the only way to correct the fraction
    of a sample that peak centring cannot reach. The shift is circular, which
    is harmless here: the impulse sits at the centre of a much longer window
    and the shifts involved are a fraction of a millisecond."""
    length = len(signal)
    spectrum = np.fft.rfft(signal)
    freq = np.fft.rfftfreq(length)
    return np.fft.irfft(spectrum * np.exp(-2j * np.pi * freq * shift), length)


class PCA:
    """PCA decomposition of a set of impulse responses (pyDRC-free)."""

    def __init__(self, impulse_dir, output_len, align="peak",
                 impulse_glob="*_impulse_*.wav"):
        self.impulse_dir = impulse_dir
        self.output_len = output_len
        self.align = align

        # Only the measured responses, matched by name: DRC leaves its own
        # outputs (rms.wav, rps.wav) in this same directory, and taking every
        # .wav would feed those back in as if they were measurements on any
        # re-run. Sorted for reproducible component ordering.
        self.DataFileList = []
        for path in sorted(glob.glob(os.path.join(impulse_dir, impulse_glob))):
            print(f"Found impulse file {os.path.basename(path)}")
            self.DataFileList.append(path)
        self.Size = len(self.DataFileList)
        self.SampleRate = None
        self.DataArray = np.zeros((self.Size, self.output_len), np.double)
        self.PCAArray = None

        # Nothing to decompose with fewer than 2 impulses: skip the (costly)
        # reading/windowing; main() reports the skip and exits cleanly.
        if self.Size < 2:
            return

        max_length = 0
        for i, path in enumerate(self.DataFileList):
            print("---------------------------------------------------------------------------------")
            print(f"\tProcessing impulse file: {path}")
            signal, samplerate = read_impulse(path)
            if self.SampleRate is None:
                self.SampleRate = samplerate
            elif samplerate != self.SampleRate:
                raise SystemExit(
                    f"Sample-rate mismatch: {path} is {samplerate} Hz but "
                    f"{self.SampleRate} Hz was expected.")
            max_length = max(max_length, len(signal))
            peak_pos = int(np.argmax(np.abs(signal)))
            print(f"\t\tLength: {len(signal)} samples. Sample peak at: {peak_pos}")
            self.DataArray[i] = center_on_peak(signal, peak_pos, self.output_len)
        print(f"Data File max length: {max_length}")

        # Peak centring leaves a residual misalignment of up to a sample, which
        # the covariance sees as variance and spends a whole component on. The
        # refinement happens on the centred impulses and before the window, so
        # every impulse still sees the same window afterwards.
        if self.align == "xcorr":
            self.align_by_covariance()

        self.DataArray *= np.blackman(self.output_len)

    def align_by_covariance(self):
        """Refine the peak centring by maximising the covariance between every
        pair of impulses (see `pairwise_lags`)."""
        half = int(round(XCORR_HALF_WINDOW_MS * self.SampleRate / 1000.0))
        max_lag = max(1, int(round(XCORR_MAX_LAG_MS * self.SampleRate / 1000.0)))
        centre = (self.output_len - 1) // 2
        lo, hi = max(0, centre - half), min(self.output_len, centre + half)
        if hi - lo < 2 * max_lag:
            print("Alignment: output_len is too short for the correlation "
                  "window; keeping the peak alignment.", file=sys.stderr)
            return

        print("---------------------------------------------------------------------------------")
        print(f"Aligning by covariance maximisation: {self.Size} impulses, "
              f"correlation window +/-{XCORR_HALF_WINDOW_MS:g} ms, "
              f"lag bound +/-{XCORR_MAX_LAG_MS:g} ms")
        lags, hit_bound, quality = pairwise_lags(self.DataArray[:, lo:hi], max_lag)
        if hit_bound:
            print("\tWARNING: some pairwise lag reached the bound; the "
                  "correlation may have locked onto a different arrival.",
                  file=sys.stderr)
        for i, lag in enumerate(lags):
            print(f"\t{os.path.basename(self.DataFileList[i])}: "
                  f"{lag:+.3f} samples ({lag / self.SampleRate * 1e6:+.1f} us)")
            self.DataArray[i] = fractional_shift(self.DataArray[i], -lag)
        print(f"\tResidual misalignment corrected: "
              f"{np.sqrt(np.mean(lags ** 2)):.3f} samples rms, "
              f"{np.abs(lags).max():.3f} samples max")
        # How alike the impulses are, as a description of the measurement set:
        # it tracks the variance the principal component will explain. It is
        # not a pass/fail figure, so nothing is judged on it here.
        print(f"\tPairwise similarity at the chosen lags: "
              f"{quality.mean():.3f} mean, {quality.min():.3f} min")

    def process(self):
        """Mean-subtract, build the covariance matrix and project onto its
        eigenvectors (same steps as pyDRC's pca.py)."""
        TrArray = self.DataArray - self.DataArray.mean(axis=1, keepdims=True)

        ER = np.cov(TrArray)
        # Covariance is real-symmetric: use eigh, which returns real eigenvalues
        # in ascending order. np.linalg.eig gives NO ordering guarantee, so the
        # old code's assumption that index 0 was the principal component was
        # fragile. Sort descending so component 0 is always the principal one.
        eigvalues, eigvectors = np.linalg.eigh(ER)
        order = np.argsort(eigvalues)[::-1]
        eigvalues = eigvalues[order]
        eigvectors = eigvectors[:, order]
        # Report each eigenvalue (variance carried by the component) and its
        # explained fraction, numbered 0..N-1 to match the saved PCA_<n>.wav
        # files and the article figures.
        total = eigvalues.sum()
        print("EigenValues (variance) and explained fraction:")
        for i in range(len(eigvalues)):
            frac = 100.0 * eigvalues[i] / total if total != 0 else 0.0
            print("%3d  %.3e  %6.2f %%" % (i, eigvalues[i], frac))
        # Project the (mean-subtracted) impulses onto the eigenvectors; row i of
        # PCAArray is the i-th component waveform (a length-output_len signal).
        self.PCAArray = np.dot(np.transpose(eigvectors), TrArray)

    def save_PCA_components(self, pca_dir, normalize=True):
        """Save every PCA component as <pca_dir>/PCA_<n>.wav, with the polarity
        corrected, numbered by their order in the algorithm starting at 0.

        If `normalize` is True, every component is divided by the peak of the
        principal component (so the principal peaks at 1.0); otherwise the raw
        PCA values are written (matching pyDRC's save_main_component scale)."""
        if self.PCAArray is None:
            raise RuntimeError("process() must be called before saving.")
        os.makedirs(pca_dir, exist_ok=True)
        principal = self.PCAArray[0]
        # Correct polarity: if the principal component's peak is negative, flip
        # the sign of every component so the main impulse points upwards.
        sign = 1.0 if principal.max() == np.abs(principal).max() else -1.0
        maxvalue = np.abs(principal).max() if normalize else 1.0
        if maxvalue == 0:
            maxvalue = 1.0
        for i in range(self.Size):
            out_path = os.path.join(pca_dir, f"PCA_{i}.wav")
            print(f"Saving PCA vector {i} to {out_path}")
            component = sign * self.PCAArray[i] / maxvalue
            # 32-bit float (matches pyDRC's output) to avoid 16-bit PCM
            # clipping/quantization of the raw PCA values.
            sf.write(out_path, component.astype(np.float32), self.SampleRate, subtype="FLOAT")


def band_gain_db(fir, sample_rate, band_lo, band_hi):
    """Energy-average gain of `fir` over [band_lo, band_hi), in dB, and the
    spread of its magnitude response inside that band.

    The gain is the mean of |H|^2 over the band, i.e. exactly the gain the
    filter applies to band-limited white noise, which is what makes it
    comparable between ways. The spread (standard deviation of the magnitude
    in dB, sampled on a log-frequency grid so that no octave dominates) says
    how meaningful that single number is: the flatter the filter is inside the
    band, the less the level reference depends on the signal used to judge it.

    The FFT is zero-padded to at least 32768 points so that even a short
    filter gets enough bins inside a narrow reference band."""
    fft_len = max(32768, 1 << (int(np.ceil(np.log2(max(len(fir), 2)))) + 2))
    spectrum = np.abs(np.fft.rfft(fir, fft_len))
    freq = np.fft.rfftfreq(fft_len, 1.0 / sample_rate)
    band = (freq >= band_lo) & (freq < band_hi)
    if not band.any():
        raise ValueError(f"no FFT bin falls inside {band_lo}-{band_hi} Hz.")
    power = np.mean(spectrum[band] ** 2)
    if not power > 0.0:
        raise ValueError(f"the filter has no energy in {band_lo}-{band_hi} Hz.")
    # geomspace needs a positive lower bound, and a band reaching past Nyquist
    # must not index past the last bin.
    log_grid = np.geomspace(max(band_lo, freq[1]), min(band_hi, freq[-1]), 200)
    sampled = spectrum[np.clip(np.searchsorted(freq, log_grid), 0, len(freq) - 1)]
    spread = np.std(20.0 * np.log10(np.maximum(sampled, 1e-30)))
    return 10.0 * np.log10(power), spread


def filter_headroom(fir, sample_rate):
    """Return (peak sample, L1 norm in dB, highest magnitude gain in dB).

    The three figures a convolver needs to be given enough headroom: the peak
    sample, the sum of absolute values (worst case for any input bounded by
    1.0) and the highest gain of the magnitude response between 20 Hz and
    20 kHz (or Nyquist, whichever is lower)."""
    fft_len = max(32768, 1 << (int(np.ceil(np.log2(max(len(fir), 2)))) + 2))
    spectrum = np.abs(np.fft.rfft(fir, fft_len))
    freq = np.fft.rfftfreq(fft_len, 1.0 / sample_rate)
    audio = (freq >= 20.0) & (freq <= min(20000.0, sample_rate / 2.0))
    peak_gain = spectrum[audio].max() if audio.any() else spectrum.max()
    return (np.abs(fir).max(),
            20.0 * np.log10(np.sum(np.abs(fir))),
            20.0 * np.log10(peak_gain))


def measured_band_powers(paths, band_lo, band_hi, fft_len):
    """In-band power of every measured impulse response, on a common grid.

    Returns the per-position powers and the spectra, so the caller can reuse
    the transforms to work out what the filter does to the same measurements
    without transforming them twice."""
    powers = []
    spectra = []
    rate = None
    for path in paths:
        signal, sample_rate = sf.read(path, dtype="float64")
        if signal.ndim > 1:
            signal = signal[:, 0]
        if rate is None:
            rate = sample_rate
        elif sample_rate != rate:
            raise SystemExit(f"'{path}' is at {sample_rate} Hz and the previous "
                             f"measurements at {rate} Hz.")
        spectrum = np.abs(np.fft.rfft(signal, fft_len))
        spectra.append(spectrum)
        freq = np.fft.rfftfreq(fft_len, 1.0 / rate)
        band = (freq >= band_lo) & (freq < band_hi)
        if not band.any():
            raise SystemExit(f"no FFT bin falls inside {band_lo}-{band_hi} Hz.")
        powers.append(np.mean(spectrum[band] ** 2))
    return np.array(powers), np.array(spectra), rate


def level_normalize(path, impulse_paths, band_lo, band_hi, suffix,
                    in_place, dry_run):
    """Scale one filter so that the way it corrects keeps its measured level.

    The filter is scaled so that the in-band power its way delivers AFTER
    correction equals the one it delivered when measured. Both are power
    averages over the measurement positions: the arithmetic mean of the
    per-position powers, which is the energy the way puts into the listening
    region, rather than the mean of the per-position level differences in dB,
    which weights every position alike regardless of how much it contributes.

    Nothing is assumed about the shape of the filter. The gain is worked out
    from what the filter actually does to the responses that were measured,
    which is why this is exact where normalising the filter against white
    noise is only an approximation: a correction filter is roughly the inverse
    of the response it corrects, so the two are anti-correlated inside the
    band and the error depends on how much each way's response varies there."""
    fir, sample_rate = sf.read(path, dtype="float64")
    if fir.ndim > 1:
        raise SystemExit(f"'{path}' has {fir.shape[1]} channels; "
                         f"filters must be mono.")
    longest = max([len(fir)] + [sf.info(p).frames for p in impulse_paths])
    fft_len = 1 << int(np.ceil(np.log2(2 * max(longest, 2))))

    powers, spectra, rate = measured_band_powers(impulse_paths, band_lo,
                                                 band_hi, fft_len)
    if rate != sample_rate:
        raise SystemExit(f"'{path}' is at {sample_rate} Hz and its measurements "
                         f"at {rate} Hz.")
    freq = np.fft.rfftfreq(fft_len, 1.0 / sample_rate)
    band = (freq >= band_lo) & (freq < band_hi)
    filter_spectrum = np.abs(np.fft.rfft(fir, fft_len))

    measured = powers.mean()
    corrected = np.mean([np.mean((filter_spectrum[band] * s[band]) ** 2)
                         for s in spectra])
    if not measured > 0.0 or not corrected > 0.0:
        raise SystemExit(f"'{path}': no energy in {band_lo}-{band_hi} Hz, in the "
                         f"filter or in its measurements. The reference band "
                         f"must lie inside the way's passband.")
    gain = np.sqrt(measured / corrected)
    scaled = fir * gain
    peak, l1_db, peak_gain_db = filter_headroom(scaled, sample_rate)
    # How much the way's level varies from one position to another: how well
    # defined the level being preserved is in the first place.
    spread = np.std(10.0 * np.log10(np.maximum(powers, 1e-30)))

    out_path = path
    if not in_place:
        stem, ext = os.path.splitext(path)
        out_path = stem + suffix + ext
    if not dry_run:
        sf.write(out_path, scaled.astype(np.float32), sample_rate, subtype="FLOAT")
    return {"path": path, "out": out_path, "count": len(impulse_paths),
            "measured": 10.0 * np.log10(measured),
            "corrected": 10.0 * np.log10(corrected),
            "applied": 20.0 * np.log10(gain), "spread": spread,
            "peak": peak, "l1_db": l1_db, "peak_gain_db": peak_gain_db}


def run_level_normalize(parsed):
    """--level-normalize mode: rescale the given filters and report."""
    jobs = []
    for path in parsed.level_normalize:
        if not os.path.isfile(path):
            raise SystemExit(f"'{path}' is not a file. --level-normalize takes "
                             f"filter files only; impulse_dir and output_len "
                             f"do not apply to it.")
        # The measurements of a way live beside its filter, which is where the
        # measurement script leaves both.
        directory = os.path.dirname(os.path.abspath(path))
        found = sorted(glob.glob(os.path.join(directory, parsed.impulse_glob)))
        found = [p for p in found if os.path.abspath(p) != os.path.abspath(path)]
        if not found:
            raise SystemExit(
                f"no measurement matching '{parsed.impulse_glob}' next to "
                f"'{path}'. The level reference is taken from the responses the "
                f"way measured, so they have to be there; use --impulse-glob if "
                f"they are named differently.")
        jobs.append((path, found))

    band_lo, band_hi = parsed.ref_band
    if not 0.0 <= band_lo < band_hi:
        raise SystemExit(f"invalid reference band {band_lo}-{band_hi} Hz.")

    print(f"Level normalization, reference band {band_lo:g}-{band_hi:g} Hz "
          f"(policy: each way keeps the in-band level it measured)")
    if parsed.dry_run:
        print("Dry run: nothing is written.")
    rows = [level_normalize(path, impulses, band_lo, band_hi, parsed.suffix,
                            parsed.in_place, parsed.dry_run)
            for path, impulses in jobs]

    # One filter per way, and they are usually all called rms.wav: label each
    # row with its parent directory when the file names alone would not tell
    # the ways apart.
    names = [os.path.basename(r["path"]) for r in rows]
    if len(set(names)) < len(names):
        names = [os.path.join(os.path.basename(os.path.dirname(os.path.abspath(r["path"]))),
                              os.path.basename(r["path"])) for r in rows]
    width = max([len("filter")] + [len(n) for n in names])
    print(f"{'filter':<{width}}  {'pos':>3}  {'measured':>11}  {'corrected':>11}  "
          f"{'applied':>11}  {'pos spread':>10}  {'peak':>7}  {'sum|h|':>11}  "
          f"{'max gain':>11}  output")
    for name, r in zip(names, rows):
        print(f"{name:<{width}}  {r['count']:3d}  {r['measured']:+8.2f} dB  "
              f"{r['corrected']:+8.2f} dB  {r['applied']:+8.2f} dB  "
              f"{r['spread']:7.2f} dB  {r['peak']:7.3f}  {r['l1_db']:+8.2f} dB  "
              f"{r['peak_gain_db']:+8.2f} dB  "
              f"{'(dry run)' if parsed.dry_run else r['out']}")

    clipping = [n for n, r in zip(names, rows) if r["peak"] > 1.0]
    if clipping:
        print("WARNING: peak sample above 1.0 in " + ", ".join(clipping)
              + "; some convolvers and file formats need it below 1.0.")
    print(f"Leave at least {max(r['peak_gain_db'] for r in rows):.1f} dB of "
          f"digital headroom before the convolver.")
    print("Each way now delivers, after correction, the in-band level it "
          "delivered when measured, so the relative levels between ways are "
          "the ones the system had at measurement time -- which is the "
          "reference, and is only as good as the balance the system had then. "
          "The 'pos spread' column is how much that level varied from one "
          "measurement position to another.")


def main():
    parser = argparse.ArgumentParser(
        description="PCA of impulse responses for DRC (no pyDRC, no plots).")
    parser.add_argument("impulse_dir", nargs="?",
                        help="Directory with the measured impulse responses in WAV format.")
    parser.add_argument("output_len", type=int, nargs="?",
                        help="Length (in samples) of the generated WAVs; each impulse is "
                             "centred on its peak to this length.")
    parser.add_argument("--normalize", type=str2bool, default=True,
                        metavar="true|false",
                        help="Normalize the components by the peak of the principal one (true) "
                             "or save the raw PCA values (false). Default: true")
    parser.add_argument("--impulse-glob", default="*_impulse_*.wav", metavar="PATTERN",
                        help="How the measured impulse responses are named. Applies both to "
                             "the PCA and to --level-normalize, and keeps DRC's own outputs "
                             "(rms.wav, rps.wav), which sit in the same directory, from being "
                             "read back in as measurements. Default: *_impulse_*.wav")
    parser.add_argument("--align", choices=["peak", "xcorr"], default="peak",
                        help="How the impulses are aligned before the PCA: 'peak' centres each "
                             "one on its own sample peak; 'xcorr' refines that centring to a "
                             "fraction of a sample by maximizing the covariance between every "
                             "pair of impulses. Default: peak")
    level = parser.add_argument_group(
        "level normalization",
        "Rescale finished DRC filters (rms.wav) to a defined level reference "
        "instead of running the PCA. The PCA + DRC chain loses the level "
        "relation between ways; this restores it from the measurements "
        "themselves.")
    level.add_argument("--level-normalize", nargs="+", metavar="FILTER.WAV",
                       help="Scale each given filter so that its way keeps, after correction, "
                            "the in-band level it measured. The measurements are read from "
                            "the filter's own directory. Runs on its own: no PCA is performed.")

    level.add_argument("--ref-band", nargs=2, type=float, default=(200.0, 2000.0),
                       metavar=("LO", "HI"),
                       help="Reference band in Hz. Default: 200 2000")
    level.add_argument("--suffix", default="_lvl", metavar="TEXT",
                       help="Suffix for the scaled filters. Default: _lvl")
    level.add_argument("--in-place", action="store_true",
                       help="Overwrite the input filters instead of writing new files.")
    level.add_argument("--dry-run", action="store_true",
                       help="Report the scaling that would be applied, write nothing.")
    parsed = parser.parse_args()

    if parsed.level_normalize:
        if parsed.impulse_dir is not None or parsed.output_len is not None:
            raise SystemExit("--level-normalize runs on its own: do not pass "
                             "impulse_dir/output_len with it.")
        run_level_normalize(parsed)
        return
    if parsed.impulse_dir is None or parsed.output_len is None:
        parser.error("impulse_dir and output_len are required "
                     "(or use --level-normalize).")

    if not os.path.isdir(parsed.impulse_dir):
        raise SystemExit(f"'{parsed.impulse_dir}' is not a directory.")
    if parsed.output_len <= 0:
        raise SystemExit("output_len must be a positive integer.")

    pca = PCA(parsed.impulse_dir, parsed.output_len, align=parsed.align,
              impulse_glob=parsed.impulse_glob)
    # PCA needs at least 2 impulses (one observation has no covariance). With
    # fewer, skip with a warning but exit cleanly so a batch pipeline continues.
    if pca.Size < 2:
        print(f"WARNING: only {pca.Size} impulse(s) matching "
              f"'{parsed.impulse_glob}' in '{parsed.impulse_dir}'; PCA needs at "
              f"least 2. Use --impulse-glob if they are named differently. "
              f"Skipping PCA.", file=sys.stderr)
        return
    pca.process()
    # Output goes into a 'pca4drc' subdirectory of the input directory.
    pca_dir = os.path.join(parsed.impulse_dir, "pca4drc")
    pca.save_PCA_components(pca_dir, normalize=parsed.normalize)
    print(f"Done. {pca.Size} PCA components written to {pca_dir}")


if __name__ == "__main__":
    main()
