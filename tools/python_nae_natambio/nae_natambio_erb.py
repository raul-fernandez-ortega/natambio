# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
nae_natambio_erb.py — NAE with a per-ERB-band PCA, the bank limited by the NAE
analysis window (experiment).

Variant of `nae_natambio.py`. Everything is kept identical to that script —
signal handling, mid/side construction, `alpha`/`beta` mode and `pan`, the
`covsteps` buffers, the eigenvector convention and ordering, the temporal
projection, the C1/C2 overlap-add reconstruction and the gains — except for a
single point:

    the one broadband eigenvector pair V(t) is replaced by one pair V_b(t) per
    band, and C1/C2 are the sum over bands of each band's reconstruction.

        L/R -> ERB-like bank -> per-band NAE PCA -> per-band C1/C2 -> sum

Nothing else is made per-band. `beta` (the `pan` factor) stays the single global
value the broadband NAE uses for that window, as does the frame size, the
covariance window and the eigenvalue ordering.

THE BANK IS LIMITED BY THE WINDOW, NOT BY THE EAR. NAE estimates its covariance
over

    N_PCA = frame_size * covsteps

samples, and a window of N samples cannot resolve detail finer than fs/N however
the bank is built. The band widths are therefore floored:

    B_min = alpha * fs / N_PCA
    B_b   = max( ERB(f_b), B_min )

so the bank is ERB-wide where the window can support it and stops narrowing
below that. At 48 kHz with frame_size 256 and covsteps 3, N_PCA = 768, T = 16 ms
and B_min = 62.5 Hz at alpha = 1, which crosses ERB(f) around 350 Hz: above it
the bank is the ERB bank, below it the bands are as wide as the window can see.
Centres are spaced to match, so the low end is covered by a few wide bands
rather than by many that the window could not tell apart. `--low-band-mode` goes
further and replaces the whole low region with one wide band.

The bank is designed on a zero-padded grid of `--nfft` bins. That grid buys
precision in PLACING and SHAPING the masks; it buys no statistical resolution,
which stays fixed at N_PCA. It also bounds the kernels: each band is realised as
an `--nfft`-tap linear-phase FIR, and the run reports the time spread of the
longest of them against the NAE window, since a band whose kernel is longer than
the window it is analysed in is a band the analysis cannot honour.

Phase A only (section 9): the shapes are gammatone-LIKE magnitudes, not causal
gammatone filters. For each band a positive magnitude A_b(f) is normalised point
by point,

    W_b(f) = A_b(f) / (sum_j A_j(f) + eps)     =>     sum_b W_b(f) = 1

and every band is given the SAME linear phase, so

    H_b(f) = W_b(f) exp(-j 2 pi f T0)   =>   sum_b H_b(f) = exp(-j 2 pi f T0)

with T0 = nfft/2 samples, the common group delay of every band. The bank is
therefore perfect-reconstruction BY CONSTRUCTION, sum_b x_b(t) = x(t - T0), and
cannot itself be the source of any artefact. T0 is compensated when the band
signals are sliced, so what reaches the PCA — and every figure compared against
the broadband run — is aligned with the input.

Three ways of getting the bands, chosen with `--filtering`:

  * `whole` (default) filters the entire signal with each band's FIR and then
    runs the NAE loop per band. Exact, artefact-free, and not realisable: it
    needs the whole recording before it can produce the first band. It is the
    reference the other two are measured against.

  * `block` is the per-window loop: one transform pair per covariance window,
    every band from it, the loops inverted. The shape a real-time engine has,
    with the bank still in the audio path.

  * `matrix` puts no bank in the audio path at all. The per-band covariances
    come from the window's own spectrum weighted by W_b(f)^2 (Parseval), and the
    reconstruction is one frequency-dependent 2x2 matrix, G1(f) = sum_b W_b(f)
    P1_b, since each band reconstructs through a constant projector. The
    transform is taken over the covariance window itself, with no zero-padding,
    so the added latency is zero: the engine already holds that window. This is
    the formulation to port.

A WARNING ABOUT THE OVERLAP-ADD. `nae_natambio.py` and `src/nae.cpp` do not use
the same reconstruction. The engine accumulates each sample slot over covsteps
successive windows, emits the oldest frame divided by covsteps+1, then shifts
and clears the top frame. The script instead adds its whole accumulator into the
output every window and carries it forward divided by covsteps. With one fixed
axis and the same input the two differ by 0.6 against an RMS of 1.0, so this is
a difference in the algorithm and not in the arithmetic. `whole` and `block`
follow the script, because they are variants of it; `matrix` follows the engine,
because it exists to predict the engine. Consequently the C1/C2 figures of
`matrix` compare only within `matrix` -- its own broadband reference is computed
through the same overlap-add -- while the axis figures (theta, its stability and
continuity, lambda1/lambda2) compare across all three, coming from the
covariance, which no overlap-add touches.

Two controls, both mandatory, both run by default:

  * Reconstruction (section 14). `sum_b W_b(f) = 1` is checked on the design grid
    before any PCA runs, and the run aborts if it does not hold; the summed band
    signals are then compared against the input in the time domain (max error,
    RMS, correlation, error spectrum).

  * Structural (section 15). `--same-axis true` forces every band to use the
    broadband eigenvectors V(t) instead of its own. Because the reconstruction
    is linear in the mid/side signal once V is fixed, and because the bank sums
    back to the input, the summed bands must then reproduce the broadband NAE to
    numerical precision. If they do not, the difference comes from the bank and
    not from the per-band PCA.

The broadband NAE is always run first, both as the reference for those controls
and for the comparison of section 20.

Metrics written for every run (sections 17 and 20): theta_b(t) = atan2(v_S, v_M)
of the principal axis as a time x band map, its temporal stability and spectral
continuity, the degeneracy ratio q_b = lambda1/lambda2 with the fraction of
band/window cells below 1.2, 1.5 and 2, the effective number of bands, and the
correlation, residual and relative C2/C1 level against the broadband run. No
heuristic acts on any of them: they are measured and reported, nothing more.

`--alpha` and `--delta-erb` both take lists, and the run sweeps their product
(section 19), writing a summary table to stdout and to CSV with one row per
combination.

On the eigenvector sign (section 18): the sign IS made consistent, but only for
the metrics. The C1/C2 reconstruction of the original NAE is a rank-1 projector,
v (v^T x), which is exactly invariant under v -> -v, so no sign convention can
change the audio of this script or of `nae_natambio.py`. Sign consistency here
buys a readable theta map, not a different output.

Because it relies only on numpy, soundfile and matplotlib (all cross-platform),
it runs on **GNU/Linux** and **MS Windows** without modification.

Usage:
    python nae_natambio_erb.py <file.wav> [--mode alpha|beta]
                                          [--analysis true|false]
                                          [--frame-size N] [--covsteps N]
                                          [--alpha A [A ...]]
                                          [--delta-erb D [D ...]]
                                          [--erb-bandwidth B] [--erb-order N]
                                          [--erb-fmin F] [--erb-fmax F]
                                          [--eta E] [--nfft N]
                                          [--low-band-mode MODE]
                                          [--same-axis true|false]
                                          [--check-only true|false]

Outputs are tagged with the bank setting (`<input>_erb_a1_d1_c1.wav`, ...) so a
per-band run never overwrites the broadband one and combinations never overwrite
each other.

This directory (tools/python_nae_natambio) is a self-contained Python project:
it shares no code with the rest of the repository.
"""

import os
import sys
import csv
import argparse
import soundfile as sf
import numpy as np
import matplotlib.pyplot as plt

# Scale the font size of every plot by 1.25. The other text sizes (titles,
# labels, ticks, legend) default to relative values, so scaling the base
# font.size scales them all by the same x1.25 factor.
plt.rcParams.update({"font.size": plt.rcParams["font.size"] * 1.25})

# Figure size for every plot (shown and saved as PNG): 16:9 aspect ratio,
# scaled by FIG_SCALE for larger output. Base 16:9 is (8, 4.5); x2 -> (16, 9).
FIG_SCALE = 2
FIGSIZE = (8 * FIG_SCALE, 4.5 * FIG_SCALE)

ANALYSIS = True
COVSTEPS = 3
ICORRL = 20
NATAMBCOEFF = -2.5
MODE = "alpha"   # default NAE mode: alpha (main) / beta (ambient)

# ERB bank defaults. DELTA_ERB is the spacing between band centres on the
# ERB-rate scale and ERB_BANDWIDTH the width of each filter in ERB units: the
# proposal insists these are independent, and they are kept so -- centres 2 ERB
# apart with filters 1 ERB wide is a legitimate configuration.
DELTA_ERB = 1.0
ERB_BANDWIDTH = 1.0
ERB_ORDER = 4
ERB_FMIN = 20.0
ERB_FMAX = 20000.0

# The width floor of section 4: B_min = ALPHA * fs / N_PCA, the resolution the
# covariance window can actually support. ALPHA = 1 takes it at face value;
# 1.5 and 2 ask the bank to stay comfortably above it.
ALPHA = 1.0

# The centre spacing is floored at ETA * B_min (section 7), so that centres do
# not end up closer together than the bands are wide -- which would be a bank of
# near-duplicate bands with a PCA each, and no more information than one.
ETA = 1.0

# Bins of the grid the bank is DESIGNED on, and taps of the linear-phase FIR
# each band becomes. Zero-padding here places and shapes the masks precisely
# (section 2); it does not add statistical resolution, which stays at N_PCA.
NFFT_DESIGN = 4096

# Section 8: the low region as one wide band instead of several the window
# cannot tell apart. The "20" of the names is --erb-fmin, whatever it is set to.
LOW_BAND_MODES = ("continuous", "one_band_20_250", "one_band_20_400")
LOW_BAND_CUTOFF = {"continuous": None, "one_band_20_250": 250.0, "one_band_20_400": 400.0}

# The frame size that goes with COVSTEPS above. Both come from section 1: the
# window NAE actually uses, 256 x 3 = 768 samples, 16 ms at 48 kHz. They differ
# from nae_natambio.py's 1024 x 5, and deliberately -- the whole revision is
# about the bank being conditioned by this window, so the window it is
# conditioned by has to be the real one. Two caveats worth knowing: natambio's
# own <steps_length> defaults to 5 rather than 3 (naconf.cpp), and the frame
# size is whatever period JACK is running, so a run meant to match a particular
# system should pass --frame-size and --covsteps to say which one.
FRAME_SIZE = 256

# The 1.019 of Patterson/Holdsworth: the factor that makes a 4th-order
# gammatone's equivalent rectangular bandwidth come out at ERB(f_c).
GAMMATONE_ERB_FACTOR = 1.019

# The epsilon of the point-by-point normalisation. It is in the formula for the
# case sum_j A_j(f) -> 0, which does not arise here: a gammatone magnitude decays
# as a power of the frequency distance and never reaches zero, so the sum stays
# of order one over the whole axis (0.4 at DC, 0.1 at Nyquist for a bank covering
# 20 Hz to 20 kHz). It is kept this small so that it costs no reconstruction.
PR_EPS = 1.0e-30

# What the reconstruction control is allowed to be off by, relative to the peak
# of the input. Float64 and a whole-signal FFT put the actual figure fifteen
# orders of magnitude below this; anything near it is a bug in the bank.
RECON_TOL = 1.0e-9

# The degeneracy thresholds of section 17: the fractions reported are of cells
# whose lambda1/lambda2 falls below each of these.
DEGENERACY_THRESHOLDS = (1.2, 1.5, 2.0)


def str2bool(value):
    """Parse an inline 'true'/'false' argument into a bool."""
    if isinstance(value, bool):
        return value
    if str(value).lower() in ("true", "1", "yes", "y", "t", "on"):
        return True
    if str(value).lower() in ("false", "0", "no", "n", "f", "off"):
        return False
    raise argparse.ArgumentTypeError(f"Expected true/false, got '{value}'")


def mode_label(wavfile, mode, delta_erb=None, alpha=None, same_axis=False):
    """Build a title suffix with the WAV name, the NAE mode and the bank setting."""
    wavname = os.path.basename(wavfile)
    if delta_erb is None:
        return f"{wavname} — NAE {mode} (broadband)"
    axis = ", same broadband axis" if same_axis else ""
    return (f"{wavname} — NAE ERB {mode}, alpha={alpha:g}, "
            f"ΔERB={delta_erb:g}{axis}")


def save_fig(prefix, suffix):
    """Save the current figure as <prefix>_<suffix>.png (next to the WAV).

    `prefix` is the WAV path without its extension; `suffix` is a short
    descriptive tag for the plot. Does nothing if prefix is None.
    """
    if prefix is not None:
        plt.savefig(f"{prefix}_{suffix}.png", dpi=150, bbox_inches="tight")


# ---------------------------------------------------------------------------
# ERB scale and the gammatone-like analysis bank (sections 2, 3 and 5)
# ---------------------------------------------------------------------------

def hz_to_erb(f):
    """ERB-rate of a frequency in Hz: E(f) = 21.4 log10(1 + 4.37 f/1000)."""
    return 21.4 * np.log10(1.0 + 4.37 * np.asarray(f, dtype=float) / 1000.0)


def erb_to_hz(e):
    """Inverse of hz_to_erb: f(E) = (1000/4.37) (10^(E/21.4) - 1)."""
    return (1000.0 / 4.37) * (10.0 ** (np.asarray(e, dtype=float) / 21.4) - 1.0)


def erb_bandwidth(f):
    """Equivalent rectangular bandwidth at f: ERB(f) = 24.7 (4.37 f/1000 + 1)."""
    return 24.7 * (4.37 * np.asarray(f, dtype=float) / 1000.0 + 1.0)


def erb_centers(fmin, fmax, delta_erb, b_min, eta=ETA):
    """Band centres, spaced in ERB-rate but never closer than the window allows.

    Section 7. Walking up from fmin, the next centre is ΔERB away on the
    ERB-rate scale, or eta*B_min away in Hz, whichever is the larger step. Above
    the transition frequency the first term wins and the result is the ERB-rate
    uniform spacing of the original bank; below it the second wins and the
    centres spread out instead of crowding into a region the covariance window
    cannot resolve anyway.

    That is the whole point of the revision: centres closer together than the
    bands are wide would give a row of near-duplicate bands, each with its own
    PCA over the same 768 samples, and no more information between them than one
    band would have carried.
    """
    centers = []
    f = float(fmin)
    while f <= fmax:
        centers.append(f)
        step_erb = erb_to_hz(hz_to_erb(f) + delta_erb) - f
        f += max(float(step_erb), eta * b_min)
    return np.asarray(centers if centers else [float(fmin)])


def band_bandwidths(centers, bw_erb, b_min):
    """B_b = max(ERB(f_b), B_min), the boxed rule of sections 4 and 21.

    `bw_erb` scales the ERB term alone, so the filter width in ERB units stays
    the independent knob it was; the floor is an absolute figure in Hz, since it
    comes from the length of the window and not from the frequency. The 1.019 is
    Patterson/Holdsworth: it is what makes a 4th-order gammatone's equivalent
    rectangular bandwidth come out at ERB(f_c) rather than 2 % under it.
    """
    erb_term = bw_erb * GAMMATONE_ERB_FACTOR * erb_bandwidth(centers)
    return np.maximum(erb_term, b_min)


def transition_frequency(bw_erb, b_min):
    """Where ERB(f) crosses B_min: below it the bank is window-limited.

    Reported rather than used. Solving 24.7 (1 + 4.37 f/1000) * k = B_min for f,
    with k the same scaling band_bandwidths() applies. Returns 0 when the floor
    is below ERB(0) and the bank is ERB-wide everywhere.
    """
    k = bw_erb * GAMMATONE_ERB_FACTOR
    if k <= 0.0:
        return 0.0
    f = ((b_min / k) / 24.7 - 1.0) * 1000.0 / 4.37
    return max(0.0, float(f))


def gammatone_magnitude(freqs, fc, bandwidth_hz, order):
    """|H(f)| of an n-th order gammatone centred at fc.

    The standard closed form of the magnitude response of
    g(t) = a t^(n-1) exp(-2 pi B t) cos(2 pi fc t + phi):

        |H(f)| = [1 + ((f - fc)/B)^2] ^ (-n/2)

    Only the magnitude is used. The causal phase is deliberately left out: what
    is wanted here is the SHAPE of a gammatone in a bank that reconstructs
    exactly and whose bands all share one group delay, not a gammatone. Section
    9 is explicit that the exact shape matters less than the width, the overlap,
    the normalisation and the reconstruction.
    """
    return (1.0 + ((freqs - fc) / bandwidth_hz) ** 2) ** (-0.5 * order)


def lowpass_magnitude(freqs, fcut, order):
    """The shape of the single wide low band of section 8.

    A lowpass of the same rational family as the bandpass shapes, knee at fcut:

        A(f) = [1 + (f/fcut)^2] ^ (-n/2)

    A bandpass centred somewhere inside the low region would dip towards DC and
    leave the bottom of the range to be rescued by the normalisation, which is a
    roundabout way of saying "everything below fcut". This says it directly.
    """
    return (1.0 + (freqs / fcut) ** 2) ** (-0.5 * order)


def erb_masks(freqs, centers, bandwidths, order, low_cutoff=None, eps=PR_EPS):
    """The analysis bank as point-by-point normalised masks.

    Returns W with one row per band, W[b, k] the weight of band b at bin k, such
    that the columns sum to one. Normalising each filter to unit energy would
    NOT give that (section 11): sum_b |H_b|^2 = 1 does not imply sum_b H_b = 1,
    and it is the second that reconstruction needs. Dividing by the pointwise
    sum imposes it directly, at the cost of the band shapes at the edges of the
    covered range no longer being gammatone-like -- below the lowest centre and
    above the highest there is nothing to share the bin with, so the outermost
    band takes all of it. That is why the covered range is a parameter: the
    edges are put outside the region under study rather than inside it.

    With `low_cutoff` the first row is the wide low band of section 8 instead of
    a bandpass, and `centers`/`bandwidths` describe only the bands above it.
    """
    n_bands = len(centers) + (1 if low_cutoff is not None else 0)
    a = np.empty((n_bands, len(freqs)), dtype=float)
    offset = 0
    if low_cutoff is not None:
        a[0] = lowpass_magnitude(freqs, low_cutoff, order)
        offset = 1
    for i, (fc, bandwidth_hz) in enumerate(zip(centers, bandwidths)):
        a[offset + i] = gammatone_magnitude(freqs, fc, bandwidth_hz, order)
    return a / (a.sum(axis=0) + eps)


def next_pow2(n):
    """Smallest power of two >= n. numpy has no next_fast_len and this file
    keeps to numpy, soundfile and matplotlib."""
    return 1 << (int(n) - 1).bit_length()


def design_bank(samplerate, delta_erb, alpha, args, n_pca, n_cov=None, n_syn=None):
    """Build the bank: masks on the design grid, then one FIR per band.

    The order is the one sections 2, 4, 7, 10 and 11 lay down. The width floor
    comes first, because it decides both how wide the bands are and how far
    apart their centres go; the shapes are then laid on the zero-padded design
    grid, normalised bin by bin into a partition of unity, and turned into
    kernels by an inverse transform.

    The kernels are the masks' own impulse responses, circularly shifted by half
    the transform so that every band carries the SAME linear phase and the same
    group delay T0 = nfft/2 (section 10). Since the masks sum to one at every
    bin, the kernels sum to a single unit impulse at T0 -- which is section 11's
    sum_b H_b(f) = exp(-j 2 pi f T0), and is exact here rather than approximate,
    the kernels being nfft samples long by construction and not truncated.

    Returns everything the caller needs to filter, plot and report.
    """
    # The floor comes from the window the PCA is ESTIMATED over, which once
    # --cov-window decouples the two is no longer the window it is reconstructed
    # over. That is what section 4 means by N_PCA: the observation the
    # covariance is computed from.
    if n_cov is None:
        n_cov = n_pca
    b_min = alpha * samplerate / float(n_cov)
    low_cutoff = LOW_BAND_CUTOFF[args.low_band_mode]
    first_center = args.erb_fmin if low_cutoff is None else low_cutoff

    centers = erb_centers(first_center, args.erb_fmax, delta_erb, b_min, args.eta)
    if low_cutoff is not None:
        # The wide band already owns everything up to its knee, so the first
        # bandpass belongs above it, not on top of it.
        centers = centers[centers > low_cutoff]
    bandwidths = band_bandwidths(centers, args.erb_bandwidth, b_min)

    nfft = args.nfft
    freqs = np.fft.rfftfreq(nfft, 1.0 / samplerate)
    masks = erb_masks(freqs, centers, bandwidths, args.erb_order, low_cutoff)

    # One linear-phase FIR per band, all with the same group delay.
    kernels = np.fft.irfft(masks, nfft, axis=1)
    kernels = np.roll(kernels, nfft // 2, axis=1)
    delay = nfft // 2

    # The same filters on the design grid, phase included. The block loop
    # multiplies by these and not by `masks`: the masks are zero-phase, so a
    # product with them puts the band back where the segment already was, and
    # the aligned slice at T0 would be reading the silence after it. These carry
    # the common delay, so the band lands at T0 and sum_b H_b is one delta
    # there.
    mask_spectra = np.fft.rfft(kernels, nfft, axis=1)

    # The analysis grid, when it differs from the reconstruction one. Same bank
    # -- same centres, same widths -- evaluated at different frequencies, and a
    # partition of unity on each of them separately, which is all either use
    # needs. `masks` above is the reconstruction grid (main pins --nfft to the
    # reconstruction window in matrix mode).
    # The analysis grid. When it coincides with the design grid the same masks
    # serve; when it does not, the same bank is evaluated at the other
    # frequencies. It is always present, because the matrix path needs it
    # whether or not --cov-window moved it.
    if n_cov == nfft:
        masks_cov = masks
    else:
        freqs_cov = np.fft.rfftfreq(n_cov, 1.0 / samplerate)
        masks_cov = erb_masks(freqs_cov, centers, bandwidths, args.erb_order, low_cutoff)

    # The synthesis grid, and a much longer one to measure the wrap against.
    # Same bank on every grid; only the sampling of it changes.
    masks_syn = masks_ref = None
    if n_syn is not None:
        freqs_syn = np.fft.rfftfreq(n_syn, 1.0 / samplerate)
        masks_syn = erb_masks(freqs_syn, centers, bandwidths, args.erb_order, low_cutoff)
        n_ref = 4 * n_syn
        freqs_ref = np.fft.rfftfreq(n_ref, 1.0 / samplerate)
        masks_ref = erb_masks(freqs_ref, centers, bandwidths, args.erb_order, low_cutoff)

    return {
        "b_min": b_min,
        "fmin": args.erb_fmin,
        "low_cutoff": low_cutoff,
        "centers": centers,
        "bandwidths": bandwidths,
        "n_bands": masks.shape[0],
        "freqs": freqs,
        "masks": masks,
        "kernels": kernels,
        "mask_spectra": mask_spectra,
        "masks_cov": masks_cov,
        "n_cov": n_cov,
        "masks_syn": masks_syn,
        "n_syn": n_syn,
        "masks_ref": masks_ref,
        "n_ref": (4 * n_syn) if n_syn is not None else None,
        "delay": delay,
        "nfft": nfft,
        "transition_hz": transition_frequency(args.erb_bandwidth, b_min),
    }


def kernel_spread_ms(kernels, samplerate, fraction=0.99):
    """Time spread of each band's FIR: the shortest span holding `fraction` of
    its energy, in milliseconds.

    The figure that says whether the bank is honouring its own premise. A band
    whose kernel is spread over more time than the covariance window is a band
    whose content at any instant was assembled from outside the window the PCA
    is about to look at -- which is exactly what flooring the widths at
    B_min is meant to prevent. Reported, not enforced.
    """
    spreads = np.empty(kernels.shape[0])
    for b in range(kernels.shape[0]):
        energy = kernels[b] ** 2
        total = energy.sum()
        if total <= 0.0:
            spreads[b] = 0.0
            continue
        # Cumulative energy either side of the peak, which for these kernels is
        # the common delay: grow a symmetric window until it holds `fraction`.
        peak = int(np.argmax(energy))
        cumulative = 0.0
        half = 0
        n = len(energy)
        while cumulative < fraction * total and half < n:
            lo = max(0, peak - half)
            hi = min(n, peak + half + 1)
            cumulative = energy[lo:hi].sum()
            half += 1
        spreads[b] = (2 * half + 1) * 1000.0 / samplerate
    return spreads


def signal_spectra(left, right, nfft_kernel):
    """FFT the whole signal once, padded for the kernels it will be filtered by.

    Whole-signal transform rather than a block loop: section 14 asks for a
    reconstruction error at numerical precision, and one transform per channel
    gives exactly that -- it IS the block loop of section 13 done with the
    overlap handled, rather than an approximation of it with the block edges
    left in. The pad is the kernel length, so the circular convolution of the
    transform is the linear convolution of the filter.
    """
    length = next_pow2(len(left) + nfft_kernel)
    return np.fft.rfft(left, length), np.fft.rfft(right, length), length


def kernel_spectrum(kernel, length):
    """One band's FIR on the filtering grid. Hoisted out of filter_band because
    the two channels share it and transforming it twice is half the cost of a
    band for nothing."""
    return np.fft.rfft(kernel, length)


def filter_band(spectrum, kernel_spec, length, delay, num_samples):
    """One band of one channel, with the common delay taken back out.

    Every band is delayed by the same T0, so removing it costs one slice and
    leaves the band signals aligned with the input -- and therefore with the
    broadband run every figure is compared against.
    """
    filtered = np.fft.irfft(spectrum * kernel_spec, length)
    return filtered[delay:delay + num_samples]


# ---------------------------------------------------------------------------
# The NAE core, unchanged from nae_natambio.py
# ---------------------------------------------------------------------------

def pan_sequence(audio, frame_size, end, mode_amb):
    """The `pan` (beta) factor of every window, computed ONCE on the broadband
    input.

    This is the same expression as `nae_natambio.py`, evaluated ahead of the
    loop instead of inside it, because section 8 requires every band to use the
    same global beta for a given window. Computing it per band would make it
    beta_b(t), which is exactly the extra freedom this experiment is not allowed
    to take.
    """
    pans = np.ones(end // frame_size)
    if not mode_amb:
        return pans
    for w, start in enumerate(range(0, end, frame_size)):
        end_audio = start + frame_size
        lo = max(0, end_audio - ICORRL * frame_size)
        icorr = np.corrcoef(audio[lo:end_audio, 0], audio[lo:end_audio, 1])[0, 1]
        pans[w] = 0.55 + np.abs(icorr) * 0.45
    return pans


def nae_process(left, right, pan_seq, frame_size, steps, forced_vectors=None):
    """One NAE pass over a stereo pair. The algorithm of `nae_natambio.py`.

    Line for line the loop of the original script, with three differences, none
    of them in the arithmetic:

      * it takes the two channels as arguments instead of reading a WAV, so the
        broadband run and each band run go through the SAME code -- which is
        what makes the structural control of section 15 mean anything;
      * `pan` is looked up in `pan_seq` instead of being computed inline, for
        the reason given in `pan_sequence`;
      * `forced_vectors`, when given, replaces the eigenvectors of the window
        with the ones supplied (the control of section 15). The eigenvalues
        returned are still the band's own, since they are a measurement of the
        band and not of the axis used.

    Note that `pan` multiplies the whole concatenation, the carried-over `buf`
    tail included, and that tail was already scaled by the previous window's
    pan. In `beta` mode the scaling therefore compounds along the covariance
    window. That is what the original script does, and what the C and C++
    engines do; it is reproduced rather than corrected, because correcting it
    here would put a second difference into an experiment that is meant to have
    exactly one. In `alpha` mode pan is 1 and the question does not arise.

    Returns (c1, c2, vectors, values): the two components as (n, 2) arrays in
    the script's own overlapped layout, the 2x2 eigenvector matrix of every
    window, and its two eigenvalues.
    """
    num_samples = len(left)
    end = int(np.floor(num_samples / frame_size)) * frame_size
    n_out = end + frame_size * (steps - 1)

    c1 = np.zeros((n_out, 2))
    c2 = np.zeros((n_out, 2))
    signal = np.zeros([frame_size * steps, 2])
    buf = np.zeros([frame_size * (steps - 1), 2])
    p1 = np.zeros((frame_size * steps, 2))
    p2 = np.zeros((frame_size * steps, 2))

    n_windows = end // frame_size
    vectors = np.zeros((n_windows, 2, 2))
    values = np.zeros((n_windows, 2))

    for w, start in enumerate(range(0, end, frame_size)):
        end_audio = start + frame_size
        start_pca = start
        end_pca = start_pca + frame_size * steps
        pan = pan_seq[w]

        # Mid component
        signal[:, 0] = np.concatenate((buf[:, 0], left[start:end_audio] + right[start:end_audio]))
        # Side component
        signal[:, 1] = pan * np.concatenate((buf[:, 1], left[start:end_audio] - right[start:end_audio]))

        # Transpose before covariance calculation
        centered = np.transpose(signal)
        ER = np.cov(centered)
        eigvalues, eigvectors = np.linalg.eig(ER)

        # Sort eigenvectors by descending eigenvalue
        if eigvalues[0] < eigvalues[1]:
            idx = np.argsort(eigvalues)[::-1]
            eigvalues = eigvalues[idx]
            eigvectors = eigvectors[:, idx]

        values[w] = eigvalues
        if forced_vectors is not None:
            eigvectors = forced_vectors[w]
        vectors[w] = eigvectors

        PCAArray = np.transpose(eigvectors) @ np.transpose(signal)

        p1[:, 0] += np.transpose(PCAArray)[:, 0] * eigvectors[0, 0]
        p1[:, 1] += np.transpose(PCAArray)[:, 0] * eigvectors[1, 0]
        p2[:, 0] += np.transpose(PCAArray)[:, 1] * eigvectors[0, 1]
        p2[:, 1] += np.transpose(PCAArray)[:, 1] * eigvectors[1, 1]

        c1[start_pca:end_pca, 0] += (p1[:, 0] + p1[:, 1]) / (steps + 1)
        c1[start_pca:end_pca, 1] += (p1[:, 0] - p1[:, 1]) / (steps + 1)
        c2[start_pca:end_pca, 0] += (p2[:, 0] + p2[:, 1]) / (steps + 1)
        c2[start_pca:end_pca, 1] += (p2[:, 0] - p2[:, 1]) / (steps + 1)

        buf[:, ] = signal[frame_size:, ]

        p1[:, 0] = np.concatenate((p1[frame_size:, 0] / steps, np.zeros(frame_size)))
        p1[:, 1] = np.concatenate((p1[frame_size:, 1] / steps, np.zeros(frame_size)))
        p2[:, 0] = np.concatenate((p2[frame_size:, 0] / steps, np.zeros(frame_size)))
        p2[:, 1] = np.concatenate((p2[frame_size:, 1] / steps, np.zeros(frame_size)))

    return c1, c2, vectors, values


def nae_process_banded(left, right, pan_seq, frame_size, steps, mask_spectra, nfft,
                       delay, forced_vectors=None, progress=False, pca=True):
    """The per-block loop of section 13: split, PCA and recompose window by window.

    The other path (`design_bank` + `filter_band`) filters the whole signal with
    each band's FIR and then runs the NAE loop per band. It is exact, and it is
    the right reference, but it is not realisable: it needs the entire recording
    before it can produce the first band. This one is the shape a real-time
    engine has, and it is the one to judge the idea by if the idea is going into
    `src/nae.cpp`:

        for each period:
            take the covariance window, N_PCA = frame_size * covsteps samples
            zero-pad to nfft, transform once per channel
            multiply by every mask, transform back, keep the aligned N_PCA
            per band: mid/side, covariance, PCA, project, overlap-add
            sum the bands

    The loops are therefore inverted with respect to the reference: windows
    outside, bands inside, one FFT pair per window instead of one per band over
    the whole signal, and every band's overlap-add state carried side by side.

    Two consequences worth knowing before reading any figure produced here.

    The first is the circular wrap. The window is nfft/2 samples of kernel wide
    on each side and only N_PCA long, so the aligned slice is contaminated by
    what wrapped round the transform -- the price of a block that is shorter
    than the filter it is passed through. It does NOT break reconstruction:
    every band is wrapped by the same transform and the masks still sum to one
    at every bin, so the bands still add back to the window exactly. It moves
    energy BETWEEN bands, which is precisely the artefact a real-time bank would
    have and the reference path does not.

    The second is beta. The reference path reproduces `nae_natambio.py`, where
    `pan` multiplies the whole covariance window including the tail carried over
    from previous windows, so in `beta` mode the scaling compounds. `nae.cpp`
    does not do that: side_step[j] = side_weight*(l - r) is written once, for
    the frame that has just arrived, and older frames keep the weight they were
    written with. This loop follows nae.cpp, applying each window's pan to its
    own frame slot alone. In `alpha` mode pan is 1 and the two agree exactly.
    """
    n_bands = mask_spectra.shape[0]
    num_samples = len(left)
    end = int(np.floor(num_samples / frame_size)) * frame_size
    n_pca = frame_size * steps
    n_out = end + frame_size * (steps - 1)
    n_windows = end // frame_size

    c1 = np.zeros((n_out, 2))
    c2 = np.zeros((n_out, 2))
    p1 = np.zeros((n_bands, n_pca, 2))
    p2 = np.zeros((n_bands, n_pca, 2))
    vectors = np.zeros((n_bands, n_windows, 2, 2))
    values = np.zeros((n_bands, n_windows, 2))
    recon_l = np.zeros(end)
    recon_r = np.zeros(end)

    # The pan each frame slot of the window was written with, oldest first, as
    # nae.cpp's side_step holds it. Shifted by one frame every window.
    pan_slots = np.ones(steps)
    seg_l = np.zeros(n_pca)
    seg_r = np.zeros(n_pca)
    signal = np.zeros((n_pca, 2))
    slot_pan = np.ones(n_pca)

    for w, start in enumerate(range(0, end, frame_size)):
        lo = start - (steps - 1) * frame_size
        src_lo = max(0, lo)
        seg_l[:] = 0.0
        seg_r[:] = 0.0
        seg_l[src_lo - lo:] = left[src_lo:start + frame_size]
        seg_r[src_lo - lo:] = right[src_lo:start + frame_size]

        # One transform per channel, every band from the same pair.
        spec_l = np.fft.rfft(seg_l, nfft)
        spec_r = np.fft.rfft(seg_r, nfft)
        bands_l = np.fft.irfft(mask_spectra * spec_l, nfft, axis=1)[:, delay:delay + n_pca]
        bands_r = np.fft.irfft(mask_spectra * spec_r, nfft, axis=1)[:, delay:delay + n_pca]

        # Section 14, measured where this loop can actually be wrong: the bands
        # of THIS window, summed. The last frame of each window tiles the
        # signal, so writing it out builds a full-length reconstruction.
        recon_l[start:start + frame_size] = bands_l.sum(axis=0)[-frame_size:]
        recon_r[start:start + frame_size] = bands_r.sum(axis=0)[-frame_size:]

        pan_slots[:-1] = pan_slots[1:]
        pan_slots[-1] = pan_seq[w]
        for k in range(steps):
            slot_pan[k * frame_size:(k + 1) * frame_size] = pan_slots[k]

        if not pca:
            # Reconstruction control only: the split is all that is being
            # measured, so the per-band PCA is skipped rather than computed and
            # thrown away.
            continue

        for b in range(n_bands):
            signal[:, 0] = bands_l[b] + bands_r[b]
            signal[:, 1] = slot_pan * (bands_l[b] - bands_r[b])

            ER = np.cov(np.transpose(signal))
            eigvalues, eigvectors = np.linalg.eig(ER)
            if eigvalues[0] < eigvalues[1]:
                idx = np.argsort(eigvalues)[::-1]
                eigvalues = eigvalues[idx]
                eigvectors = eigvectors[:, idx]

            values[b, w] = eigvalues
            if forced_vectors is not None:
                eigvectors = forced_vectors[w]
            vectors[b, w] = eigvectors

            PCAArray = np.transpose(eigvectors) @ np.transpose(signal)
            p1[b, :, 0] += np.transpose(PCAArray)[:, 0] * eigvectors[0, 0]
            p1[b, :, 1] += np.transpose(PCAArray)[:, 0] * eigvectors[1, 0]
            p2[b, :, 0] += np.transpose(PCAArray)[:, 1] * eigvectors[0, 1]
            p2[b, :, 1] += np.transpose(PCAArray)[:, 1] * eigvectors[1, 1]

            c1[start:start + n_pca, 0] += (p1[b, :, 0] + p1[b, :, 1]) / (steps + 1)
            c1[start:start + n_pca, 1] += (p1[b, :, 0] - p1[b, :, 1]) / (steps + 1)
            c2[start:start + n_pca, 0] += (p2[b, :, 0] + p2[b, :, 1]) / (steps + 1)
            c2[start:start + n_pca, 1] += (p2[b, :, 0] - p2[b, :, 1]) / (steps + 1)

            p1[b, :, 0] = np.concatenate((p1[b, frame_size:, 0] / steps, np.zeros(frame_size)))
            p1[b, :, 1] = np.concatenate((p1[b, frame_size:, 1] / steps, np.zeros(frame_size)))
            p2[b, :, 0] = np.concatenate((p2[b, frame_size:, 0] / steps, np.zeros(frame_size)))
            p2[b, :, 1] = np.concatenate((p2[b, frame_size:, 1] / steps, np.zeros(frame_size)))

        if progress and sys.stdout.isatty() and (w % 64) == 0:
            print(f"    window {w + 1}/{n_windows}", end="\r")

    if progress and sys.stdout.isatty():
        print(" " * 60, end="\r")

    return c1, c2, vectors, values, recon_l, recon_r


def nae_process_matrix(left, right, pan_seq, frame_size, steps,
                       masks_cov, n_cov, masks_syn, n_syn,
                       masks_ref=None, n_ref=None,
                       forced_vectors=None, progress=False):
    """The formulation a real-time engine would run: no bank in the audio path.

    The band split never happens as a filter here. Two identities make it
    unnecessary, both verified to machine precision:

      * the covariance of band b over the window is the window's own spectrum
        weighted by W_b(f)^2 (Parseval), so every V_b is obtained from ONE
        transform of the window with no inverse transform and no delay;

      * each band reconstructs through a constant 2x2 projector P_b = v_b v_b^T
        applied to W_b(f)X(f), so the sum over bands is

              C1(f) = [ sum_b W_b(f) P1_b ] X(f)  =  G1(f) X(f)

        -- one frequency-dependent 2x2 matrix, whatever the number of bands.
        G1 + G2 = I, because sum_b W_b = 1 and P1_b + P2_b = I.

    So the band count never enters the transform count: two forward transforms
    per window for the analysis, a weighted sum per band, one 2x2 eigenproblem
    per band, and two forward plus four inverse transforms for the synthesis.

    THREE WINDOWS, ALL ENDING AT THE CURRENT SAMPLE AND EXTENDING BACK.

      * the reconstruction window, N_PCA = frame_size * covsteps, which is what
        the overlap-add and the latency are built on and is not touched here;

      * the analysis window, `n_cov`, over which the axes are estimated. Longer
        is steadier, and costs no latency because it is all past;

      * the synthesis window, `n_syn`, the transform G1 is applied through. This
        one is not about resolution at all: it is the headroom that keeps the
        circular product from folding onto the output.

    WHY THE SYNTHESIS WINDOW EXISTS. A product in frequency is a circular
    convolution. Applied over exactly N_PCA samples there is no headroom at all,
    so the tail of the window wraps onto its head -- and the head is the frame
    the engine emits. The wrap cancels exactly between C1 and C2, since
    G1 + G2 = I, which is why a control that checks C1 + C2 cannot see it; but
    within either component alone it is broadband aliasing, and against a
    component whose own spectrum falls steeply it ends up ABOVE the signal at
    the top of the band. Measured on `manne_his_men_i_am_in_love.wav`, applying
    over N_PCA = 768 put the aliasing 1.3 dB above C1 between 4 and 10 kHz and
    7.9 dB above it between 10 and 20 kHz. Doubling the transform to 1536 drops
    it to -61 and -53 dB and it saturates there.

    HOW THE OVERLAP-ADD SURVIVES THAT. Applying G1 is linear in G1, so summing
    the filters of the last covsteps windows and applying once is identical to
    applying each and summing -- which is what the engine's overlap-add does.
    This function therefore keeps a ring of the last covsteps sets of PER-BAND
    projector coefficients (three numbers per band, not three arrays per bin,
    since sum_k masks.T @ p_k = masks.T @ sum_k p_k), sums them, and applies the
    result once, at the moment of emission. The frame being emitted is the
    oldest of the covsteps, so it already has (covsteps-1)*frame_size samples of
    RIGHT context, free; the left context is however much of the past `n_syn`
    asks for. Nothing waits for anything: the added latency is still zero, and
    the result is what the current overlap-add would produce if it had no
    aliasing.

    `masks_ref` / `n_ref`, when given, repeat the synthesis through a much
    longer transform and report the difference. That difference IS the
    aliasing -- the metric whose absence let the artefact through.

    Pass single all-ones rows to get the broadband engine, which is what this
    mode's own reference is computed with.
    """
    n_bands = masks_cov.shape[0]
    num_samples = len(left)
    end = int(np.floor(num_samples / frame_size)) * frame_size
    n_pca = frame_size * steps
    n_out = end + frame_size * (steps - 1)
    n_windows = end // frame_size
    nbins = masks_cov.shape[1]

    # The covariance weights are W^2 and the synthesis weights are W: the first
    # is an energy, the second an amplitude.
    w2 = masks_cov ** 2

    # Turning a one-sided spectrum into the full Parseval sum: every bin counts
    # twice except DC and, for an even length, Nyquist. DC is given weight zero
    # instead of one, which removes the mean exactly -- the covariance is about
    # the variation and np.cov subtracts it too. The synthesis below uses the
    # untouched spectrum, DC included, because zeroing it there would be a
    # high-pass filter and not a statistic.
    fold = np.full(nbins, 2.0)
    fold[0] = 0.0
    if n_cov % 2 == 0:
        fold[-1] = 1.0
    cov_norm = 1.0 / (float(n_cov) * float(n_cov - 1))

    c1 = np.zeros((n_out, 2))
    c2 = np.zeros((n_out, 2))
    vectors = np.zeros((n_bands, n_windows, 2, 2))
    values = np.zeros((n_bands, n_windows, 2))
    # The emitted frame lags the current sample by covsteps-1 frames, so the
    # reconstruction it can be checked against is that much shorter than the
    # input: the last frames are still inside the engine.
    valid_len = end - frame_size * (steps - 1)
    recon_l = np.zeros(valid_len)
    recon_r = np.zeros(valid_len)

    # The ring of the last covsteps projector coefficient sets: (mm, ms, ss) per
    # band, for each of the two components. 1.4 kB at twenty bands, against the
    # 55 kB an array-per-bin ring would need.
    ring1 = np.zeros((steps, 3, n_bands))
    ring2 = np.zeros((steps, 3, n_bands))

    # All three windows end at the same sample, so one history buffer serves
    # them and each takes its own tail.
    n_hist = max(n_cov, n_syn, n_ref or 0)
    hist_frames = n_hist // frame_size
    pan_slots = np.ones(hist_frames)
    slot_pan = np.ones(n_hist)
    seg_l = np.zeros(n_hist)
    seg_r = np.zeros(n_hist)

    syn_offset = n_syn - n_pca          # where the emitted frame sits
    ref_offset = (n_ref - n_pca) if n_ref else 0
    alias_num = 0.0
    alias_den = 0.0
    alias_err = [] if n_ref else None
    alias_sig = [] if n_ref else None

    for w, start in enumerate(range(0, end, frame_size)):
        lo = start + frame_size - n_hist
        src_lo = max(0, lo)
        seg_l[:] = 0.0
        seg_r[:] = 0.0
        seg_l[src_lo - lo:] = left[src_lo:start + frame_size]
        seg_r[src_lo - lo:] = right[src_lo:start + frame_size]

        pan_slots[:-1] = pan_slots[1:]
        pan_slots[-1] = pan_seq[w]
        for k in range(hist_frames):
            slot_pan[k * frame_size:(k + 1) * frame_size] = pan_slots[k]

        mid_all = seg_l + seg_r
        side_all = slot_pan * (seg_l - seg_r)

        # --- analysis: the axes, over the covariance window
        spec_cm = np.fft.rfft(mid_all[-n_cov:], n_cov)
        spec_cs = np.fft.rfft(side_all[-n_cov:], n_cov)
        p_mm = fold * (spec_cm.real ** 2 + spec_cm.imag ** 2)
        p_ss = fold * (spec_cs.real ** 2 + spec_cs.imag ** 2)
        p_ms = fold * (spec_cm.real * spec_cs.real + spec_cm.imag * spec_cs.imag)

        mats = np.empty((n_bands, 2, 2))
        mats[:, 0, 0] = (w2 @ p_mm) * cov_norm
        mats[:, 1, 1] = (w2 @ p_ss) * cov_norm
        mats[:, 0, 1] = mats[:, 1, 0] = (w2 @ p_ms) * cov_norm
        eigvalues, eigvectors = np.linalg.eig(mats)
        swap = eigvalues[:, 0] < eigvalues[:, 1]
        if np.any(swap):
            eigvalues[swap] = eigvalues[swap][:, ::-1]
            eigvectors[swap] = eigvectors[swap][:, :, ::-1]

        values[:, w] = eigvalues
        if forced_vectors is not None:
            eigvectors = np.broadcast_to(forced_vectors[w], (n_bands, 2, 2)).copy()
        vectors[:, w] = eigvectors

        v1 = eigvectors[:, :, 0]
        v2 = eigvectors[:, :, 1]

        # --- the ring: this window's projectors in, the oldest out
        ring1[:-1] = ring1[1:]
        ring2[:-1] = ring2[1:]
        ring1[-1] = (v1[:, 0] * v1[:, 0], v1[:, 0] * v1[:, 1], v1[:, 1] * v1[:, 1])
        ring2[-1] = (v2[:, 0] * v2[:, 0], v2[:, 0] * v2[:, 1], v2[:, 1] * v2[:, 1])
        s1 = ring1.sum(axis=0)
        s2 = ring2.sum(axis=0)

        # --- synthesis: one application of the summed filter, with context
        def emit(masks, n, offset):
            spec_m = np.fft.rfft(mid_all[-n:], n)
            spec_s = np.fft.rfft(side_all[-n:], n)
            g1 = masks.T @ s1[0], masks.T @ s1[1], masks.T @ s1[2]
            g2 = masks.T @ s2[0], masks.T @ s2[1], masks.T @ s2[2]
            a = np.fft.irfft(g1[0] * spec_m + g1[1] * spec_s, n)[offset:offset + frame_size]
            b = np.fft.irfft(g1[1] * spec_m + g1[2] * spec_s, n)[offset:offset + frame_size]
            c = np.fft.irfft(g2[0] * spec_m + g2[1] * spec_s, n)[offset:offset + frame_size]
            d = np.fft.irfft(g2[1] * spec_m + g2[2] * spec_s, n)[offset:offset + frame_size]
            return a, b, c, d

        c1_mid, c1_side, c2_mid, c2_side = emit(masks_syn, n_syn, syn_offset)

        f = frame_size
        c1[start:start + f, 0] = (c1_mid + c1_side) / (steps + 1)
        c1[start:start + f, 1] = (c1_mid - c1_side) / (steps + 1)
        c2[start:start + f, 0] = (c2_mid + c2_side) / (steps + 1)
        c2[start:start + f, 1] = (c2_mid - c2_side) / (steps + 1)

        # Section 14 on the emitted frame. G1 + G2 = I, so the summed filter of
        # the ring is covsteps impulses: the two components give the input back
        # scaled by covsteps, whatever the transform length. That is exactly why
        # this control is blind to the aliasing, and why the one below exists.
        # The emitted frame is not the newest one: it is covsteps-1 frames back,
        # which is where it has to be written for the control to compare it
        # against the input samples it actually reconstructs. Before the ring
        # fills, out_start is negative and there is nothing to check yet -- which
        # happens for exactly the windows whose ring is short.
        out_start = start - frame_size * (steps - 1)
        if out_start >= 0:
            pan_frame = slot_pan[-n_syn:][syn_offset:syn_offset + f]
            rec_mid = (c1_mid + c2_mid) / steps
            rec_side = (c1_side + c2_side) / steps / pan_frame
            recon_l[out_start:out_start + f] = 0.5 * (rec_mid + rec_side)
            recon_r[out_start:out_start + f] = 0.5 * (rec_mid - rec_side)

        # The control that was missing: the same synthesis through a transform
        # long enough to be free of the wrap, and the difference between the
        # two, per component. This is what the ear heard and the sum could not.
        if n_ref:
            r1m, r1s, _, _ = emit(masks_ref, n_ref, ref_offset)
            ref_left = (r1m + r1s) / (steps + 1)
            got_left = c1[start:start + f, 0]
            alias_err.append(got_left - ref_left)
            alias_sig.append(ref_left)

        if progress and sys.stdout.isatty() and (w % 128) == 0:
            print(f"    window {w + 1}/{n_windows}", end="\r")

    if progress and sys.stdout.isatty():
        print(" " * 60, end="\r")

    alias = None
    if n_ref:
        alias = (np.concatenate(alias_err), np.concatenate(alias_sig))

    return c1, c2, vectors, values, recon_l, recon_r, alias


# ---------------------------------------------------------------------------
# Metrics (sections 14, 17 and 20). Measured and reported; none of them acts.
# ---------------------------------------------------------------------------

def consistent_signs(vectors):
    """Impose a consistent sign on the principal eigenvector (section 11).

    The reference is taken by the priority the proposal gives: the same band in
    the previous window, failing that the neighbouring band in the first window,
    and for the very first cell a fixed convention with respect to the M axis.

    Analysis only. The C1/C2 reconstruction of NAE is v (v^T x), which is exactly
    invariant under v -> -v, so nothing here can reach the audio: what it buys is
    a theta map that does not flip by 180 degrees wherever the eigensolver
    happened to return the other sign.
    """
    v = vectors[:, :, :, 0].copy()
    n_bands, n_windows = v.shape[0], v.shape[1]
    m_axis = np.array([1.0, 0.0])
    for b in range(n_bands):
        for t in range(n_windows):
            if t > 0:
                ref = v[b, t - 1]
            elif b > 0:
                ref = v[b - 1, 0]
            else:
                ref = m_axis
            if np.dot(v[b, t], ref) < 0.0:
                v[b, t] = -v[b, t]
    return v


def wrap_axis(delta):
    """Wrap an angle difference into (-90, 90] degrees' worth of radians.

    theta is the direction of an axis, not of a vector: theta and theta+pi are
    the same axis. Differences are therefore only meaningful modulo pi, and a
    raw subtraction of two angles would report a rotation of 179 degrees where
    the axis moved by one.
    """
    return (delta + 0.5 * np.pi) % np.pi - 0.5 * np.pi


def axis_angles(vectors):
    """theta_b(t) = atan2(v_S, v_M) of the sign-fixed principal eigenvector."""
    v = consistent_signs(vectors)
    return np.arctan2(v[:, :, 1], v[:, :, 0])


def degeneracy_ratio(values, eps=1.0e-20):
    """q_b(t) = lambda1 / (lambda2 + eps), the measure of section 17.

    Where q is near one the two eigenvalues are equal and the orientation of the
    axis is not defined by the data; the point of measuring it is to find out
    how much of the time/frequency plane is in that state at each ΔERB, since a
    resolution fine enough to make most cells degenerate is a resolution that is
    too fine.
    """
    return values[:, :, 0] / (values[:, :, 1] + eps)


def avg_spectrum(x, samplerate, nfft=8192):
    """Averaged periodogram of a mono signal, for the error/residual spectra.

    Hann-windowed segments with 50 % overlap, averaged in power. A single FFT of
    a two-minute signal would be a spectrum of millions of unaveraged bins, which
    is noise with a shape somewhere in it.
    """
    x = np.asarray(x, dtype=float)
    if len(x) < nfft:
        x = np.pad(x, (0, nfft - len(x)))
    window = np.hanning(nfft)
    hop = nfft // 2
    n_seg = 1 + (len(x) - nfft) // hop
    acc = np.zeros(nfft // 2 + 1)
    for i in range(n_seg):
        seg = x[i * hop:i * hop + nfft] * window
        acc += np.abs(np.fft.rfft(seg)) ** 2
    acc /= max(1, n_seg)
    return np.fft.rfftfreq(nfft, 1.0 / samplerate), acc


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=float) ** 2)))


def db(x, floor=1.0e-30):
    return 20.0 * np.log10(max(float(x), floor))


def correlation(a, b):
    """Pearson correlation of two signals, flattened over both channels."""
    a = np.asarray(a, dtype=float).ravel()
    b = np.asarray(b, dtype=float).ravel()
    if np.std(a) == 0.0 or np.std(b) == 0.0:
        return float("nan")
    return float(np.corrcoef(a, b)[0, 1])


def reconstruction_report(left, right, recon_l, recon_r, samplerate, peak):
    """The control of section 6: sum the bands straight back and measure.

    Returns a dict; the caller decides whether the run may continue. The bank is
    perfect-reconstruction by construction, so this does not test the idea -- it
    tests that the masks were built as intended, which is exactly what has to be
    true before a single per-band PCA is worth running.
    """
    err_l = left - recon_l
    err_r = right - recon_r
    err = np.concatenate([err_l, err_r])
    return {
        "max_abs": float(np.max(np.abs(err))),
        "max_rel": float(np.max(np.abs(err)) / max(peak, 1.0e-30)),
        "rms": rms(err),
        "corr_l": correlation(left, recon_l),
        "corr_r": correlation(right, recon_r),
        "err_l": err_l,
        "err_r": err_r,
    }


# ---------------------------------------------------------------------------
# Plots
# ---------------------------------------------------------------------------

def band_frequencies(bank):
    """One representative frequency per band, for labelling a band axis.

    The bands are no longer uniform on any scale -- the low end may be one wide
    band and the spacing changes where the width floor takes over -- so the
    plots are drawn against band INDEX and the frequencies become tick labels.
    A wide low band is represented by the midpoint of the range it covers on the
    ERB-rate scale, which is where it sits among its neighbours rather than
    where its knee is.
    """
    freqs = list(bank["centers"])
    if bank["low_cutoff"] is not None:
        low = erb_to_hz(0.5 * (hz_to_erb(bank["fmin"]) + hz_to_erb(bank["low_cutoff"])))
        freqs = [low] + freqs
    return np.asarray(freqs)


def band_axis(ax, bank, n_ticks=8):
    """Label a band-index axis with the frequencies the bands sit at."""
    freqs = band_frequencies(bank)
    idx = np.unique(np.linspace(0, len(freqs) - 1, n_ticks).astype(int))
    ax.set_yticks(idx)
    ax.set_yticklabels([f"{freqs[i]:.0f}" for i in idx])
    ax.set_ylabel("Frequency (Hz), one row per band")


def plot_erb_bank(bank, samplerate, label, prefix=None):
    """The bank itself: the masks, their sum, and where the width floor bites."""
    freqs = bank["freqs"]
    masks = bank["masks"]
    plt.figure(figsize=FIGSIZE)
    for b in range(masks.shape[0]):
        plt.semilogx(freqs[1:], masks[b, 1:], linewidth=0.7)
    plt.semilogx(freqs[1:], masks[:, 1:].sum(axis=0), color="black",
                 linewidth=1.5, linestyle="dashed", label="sum over bands")
    if bank["transition_hz"] > freqs[1]:
        plt.axvline(bank["transition_hz"], color="grey", linestyle="dotted",
                    linewidth=1.5,
                    label=f"ERB(f) = B_min ({bank['transition_hz']:.0f} Hz)")
    plt.xlim(20, freqs[-1])
    plt.ylim(0, 1.15)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("W_b(f)")
    plt.grid(True, which="both")
    plt.legend(loc="upper right")
    plt.title(f"ERB-like bank: {masks.shape[0]} window-limited PR masks, "
              f"B_min = {bank['b_min']:.1f} Hz\n{label}")
    save_fig(prefix, "erb_bank")
    plt.show()
    plt.close('all')


def plot_kernels(bank, spreads, window_ms, samplerate, label, prefix=None):
    """The other half of the bank: how long each band's FIR actually is.

    Left, the kernels themselves around the common delay; right, the 99 % energy
    spread of each against the NAE covariance window. The second is the plot the
    revision exists for: a band whose kernel outlasts the window is a band whose
    content at any instant came from outside what the PCA is looking at.
    """
    kernels = bank["kernels"]
    delay = bank["delay"]
    span = int(min(delay, 0.05 * samplerate))
    t = (np.arange(-span, span) / samplerate) * 1000.0
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=FIGSIZE)
    for b in range(kernels.shape[0]):
        ax1.plot(t, kernels[b, delay - span:delay + span], linewidth=0.6)
    ax1.set_xlabel("Time (ms) around the common delay T0")
    ax1.set_ylabel("h_b(t)")
    ax1.grid(True)
    ax1.set_title(f"Band kernels ({bank['nfft']} taps, linear phase)")

    ax2.plot(np.arange(len(spreads)), spreads, marker="o", markersize=3,
             linewidth=1.0, label="99 % energy spread")
    ax2.axhline(window_ms, color="red", linestyle="dashed", linewidth=1.5,
                label=f"NAE window ({window_ms:.1f} ms)")
    ax2.set_xlabel("Band index")
    ax2.set_ylabel("ms")
    ax2.grid(True)
    ax2.legend()
    ax2.set_title("Kernel spread against the analysis window")
    fig.suptitle(f"Bank in time\n{label}")
    save_fig(prefix, "erb_kernels")
    plt.show()
    plt.close('all')


def plot_reconstruction_error(report, samplerate, label, prefix=None):
    """The error spectrum of the section 14 control."""
    freqs, power = avg_spectrum(report["err_l"], samplerate)
    plt.figure(figsize=FIGSIZE)
    plt.semilogx(freqs[1:], 10.0 * np.log10(power[1:] + 1.0e-300), linewidth=0.7)
    plt.xlim(20, samplerate / 2)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("dB")
    plt.grid(True, which="both")
    plt.title("Band-sum reconstruction error spectrum (left)\n"
              f"max |e| = {report['max_abs']:.3e} ({report['max_rel']:.2e} of peak)\n{label}")
    save_fig(prefix, "erb_reconstruction_error")
    plt.show()
    plt.close('all')


def plot_theta_map(theta, bank, samplerate, frame_size, label, prefix=None):
    """theta_b(t) as a time x band map (section 20)."""
    n_bands, n_windows = theta.shape
    t = np.arange(n_windows + 1) * frame_size / samplerate
    edges = np.arange(n_bands + 1) - 0.5
    # theta is drawn as the AXIS angle, wrapped into (-90, 90]: atan2 returns
    # (-180, 180], and an axis at -170 degrees is the axis at +10. Plotting the
    # raw angle would paint the same orientation in two colours and, with a
    # scale clipped at +-90, would flatten everything beyond it into the end
    # colour. The colormap is cyclic so that -90 and +90, which ARE the same
    # axis, meet rather than sit at opposite ends of the scale.
    plt.figure(figsize=FIGSIZE)
    mesh = plt.pcolormesh(t, edges, np.degrees(wrap_axis(theta)), cmap="twilight",
                          vmin=-90, vmax=90, shading="auto")
    cbar = plt.colorbar(mesh)
    cbar.set_label("theta (degrees, axis angle)")
    plt.xlabel("Time (s)")
    band_axis(plt.gca(), bank)
    plt.title(f"Principal axis angle per band, atan2(vS, vM)\n{label}")
    save_fig(prefix, "erb_theta_map")
    plt.show()
    plt.close('all')


def plot_theta_stability(theta, bank, label, prefix=None):
    """Temporal stability per band (section 20)."""
    d = np.degrees(np.abs(wrap_axis(np.diff(theta, axis=1))))
    med = np.median(d, axis=1)
    p90 = np.percentile(d, 90, axis=1)
    p99 = np.percentile(d, 99, axis=1)
    idx = np.arange(theta.shape[0])
    plt.figure(figsize=FIGSIZE)
    plt.plot(idx, med, label="median |Δt theta|", linewidth=1.0)
    plt.plot(idx, p90, label="p90", linewidth=1.0)
    plt.plot(idx, p99, label="p99", linewidth=1.0)
    freqs = band_frequencies(bank)
    ticks = np.unique(np.linspace(0, len(idx) - 1, 8).astype(int))
    plt.gca().set_xticks(ticks)
    plt.gca().set_xticklabels([f"{freqs[i]:.0f}" for i in ticks])
    plt.xlabel("Frequency (Hz), one point per band")
    plt.ylabel("degrees")
    plt.grid(True)
    plt.legend()
    plt.title(f"Temporal stability of the per-band axis\n{label}")
    save_fig(prefix, "erb_theta_stability")
    plt.show()
    plt.close('all')
    return med, p90, p99


def plot_theta_continuity(theta, bank, label, prefix=None):
    """Spectral continuity between contiguous bands (section 20)."""
    if theta.shape[0] < 2:
        return np.array([]), np.array([])
    d = np.degrees(np.abs(wrap_axis(np.diff(theta, axis=0))))
    med = np.median(d, axis=1)
    p90 = np.percentile(d, 90, axis=1)
    idx = np.arange(len(med))
    plt.figure(figsize=FIGSIZE)
    plt.plot(idx, med, label="median |Δb theta|", linewidth=1.0)
    plt.plot(idx, p90, label="p90", linewidth=1.0)
    freqs = band_frequencies(bank)
    ticks = np.unique(np.linspace(0, len(idx) - 1, 8).astype(int))
    plt.gca().set_xticks(ticks)
    plt.gca().set_xticklabels([f"{freqs[i]:.0f}" for i in ticks])
    plt.xlabel("Frequency (Hz), between one band and the next")
    plt.ylabel("degrees")
    plt.grid(True)
    plt.legend()
    plt.title(f"Spectral continuity of the per-band axis\n{label}")
    save_fig(prefix, "erb_theta_continuity")
    plt.show()
    plt.close('all')
    return med, p90


def plot_degeneracy(q, bank, label, prefix=None):
    """Where the axis is not defined by the data (section 17)."""
    idx = np.arange(q.shape[0])
    plt.figure(figsize=FIGSIZE)
    for threshold in DEGENERACY_THRESHOLDS:
        frac = np.mean(q < threshold, axis=1) * 100.0
        plt.plot(idx, frac, label=f"q < {threshold}", linewidth=1.0)
    freqs = band_frequencies(bank)
    ticks = np.unique(np.linspace(0, len(idx) - 1, 8).astype(int))
    plt.gca().set_xticks(ticks)
    plt.gca().set_xticklabels([f"{freqs[i]:.0f}" for i in ticks])
    plt.xlabel("Frequency (Hz), one point per band")
    plt.ylabel("% of windows")
    plt.ylim(0, 100)
    plt.grid(True)
    plt.legend()
    plt.title(f"Degenerate cells per band, q = lambda1/lambda2\n{label}")
    save_fig(prefix, "erb_degeneracy")
    plt.show()
    plt.close('all')


def plot_vs_broadband(c1_erb, c2_erb, c1_ref, c2_ref, samplerate, label, prefix=None):
    """The comparison of section 20, as residual spectra.

    A large difference is NOT read as an improvement here; it is read as a
    difference. The plot exists so that the difference has a shape.
    """
    plt.figure(figsize=FIGSIZE)
    for name, a, b in (("C1", c1_erb, c1_ref), ("C2", c2_erb, c2_ref)):
        freqs, p_ref = avg_spectrum(b[:, 0], samplerate)
        _, p_res = avg_spectrum(a[:, 0] - b[:, 0], samplerate)
        plt.semilogx(freqs[1:], 10.0 * np.log10(p_ref[1:] + 1.0e-300),
                     linewidth=0.8, label=f"{name} broadband")
        plt.semilogx(freqs[1:], 10.0 * np.log10(p_res[1:] + 1.0e-300),
                     linewidth=0.8, linestyle="dashed", label=f"{name} residual (ERB - broadband)")
    plt.xlim(20, samplerate / 2)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("dB")
    plt.grid(True, which="both")
    plt.legend()
    plt.title(f"ERB run against broadband NAE, left channel\n{label}")
    save_fig(prefix, "erb_vs_broadband")
    plt.show()
    plt.close('all')


def plot_sweep(rows, label, prefix=None):
    """The sweep of section 19, as the things it is meant to trade off.

    Everything is drawn against the number of bands the combination actually
    produced rather than against ΔERB, because with the width floor in place
    ΔERB no longer decides that on its own: alpha moves it too, and section 23's
    question -- whether ten to twenty bands already give the whole benefit -- is
    asked in bands, not in ERB units.
    """
    alphas = sorted({r["alpha"] for r in rows})
    # One colour per alpha, with Δt solid and Δb dashed. Letting matplotlib
    # cycle its own colour per call would give the same alpha two colours in the
    # right-hand panel and reuse those colours for a different alpha in the
    # left-hand one, which is the one thing the reader must not misread here.
    cycle = plt.rcParams["axes.prop_cycle"].by_key()["color"]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=FIGSIZE)
    for i, alpha in enumerate(alphas):
        colour = cycle[i % len(cycle)]
        sub = sorted([r for r in rows if r["alpha"] == alpha], key=lambda r: r["n_bands"])
        bands = [r["n_bands"] for r in sub]
        ax1.plot(bands, [r["q_lt_1.5"] * 100.0 for r in sub], marker="o",
                 color=colour, linewidth=1.0, label=f"alpha = {alpha:g}")
        ax2.plot(bands, [r["dtheta_median"] for r in sub], marker="o",
                 color=colour, linewidth=1.0, label=f"alpha = {alpha:g}, Δt")
        ax2.plot(bands, [r["dbtheta_median"] for r in sub], marker="s",
                 color=colour, linestyle="dashed", linewidth=1.0,
                 label=f"alpha = {alpha:g}, Δb")
    ax1.set_xlabel("Number of bands")
    ax1.set_ylabel("% of cells with q < 1.5")
    ax1.grid(True)
    ax1.legend()
    ax1.set_title("Degenerate cells")
    ax2.set_xlabel("Number of bands")
    ax2.set_ylabel("degrees")
    ax2.grid(True)
    ax2.legend()
    ax2.set_title("Axis stability and continuity")
    fig.suptitle(f"Window-limited ERB bank sweep\n{label}")
    save_fig(prefix, "erb_sweep")
    plt.show()
    plt.close('all')


# ---------------------------------------------------------------------------
# One ERB resolution, end to end
# ---------------------------------------------------------------------------

def run_bank(delta_erb, alpha, ctx):
    """Build the bank at this (alpha, ΔERB), run the per-band NAE and measure.

    `ctx` carries everything shared across a sweep: the FFT of the input
    (computed once), the broadband reference run, and the command line. Returns
    the summary row of section 19/20, or None if the run was a reconstruction
    check only.
    """
    audio = ctx["audio"]
    samplerate = ctx["samplerate"]
    num_samples = ctx["num_samples"]
    frame_size = ctx["frame_size"]
    steps = ctx["steps"]
    args = ctx["args"]

    tag = f"erb_a{alpha:g}_d{delta_erb:g}"
    if args.mode != MODE:
        # Same convention as the fields below: the default is implicit, anything
        # else is named. Without this an alpha run and a beta run of the same
        # bank are the same filename, and the second silently replaces the first.
        tag += f"_{args.mode}"
    if args.low_band_mode != "continuous":
        tag += f"_{args.low_band_mode}"
    if args.filtering != "whole":
        tag += f"_{args.filtering}"
    if ctx["n_cov"] != ctx["n_pca"]:
        # Two runs that differ only in a window length are two different
        # experiments; without this they would be the same filename.
        tag += f"_cov{ctx['n_cov']}"
    if ctx.get("n_syn"):
        tag += f"_syn{ctx['n_syn']}"
    if args.same_axis:
        tag += "_sameaxis"
    prefix = f"{ctx['base_prefix']}_{tag}"
    label = mode_label(args.wavfile, args.mode, delta_erb, alpha, args.same_axis)

    bank = design_bank(samplerate, delta_erb, alpha, args, ctx["n_pca"], ctx["n_cov"],
                       ctx["n_syn"])
    n_bands = bank["n_bands"]
    spreads = kernel_spread_ms(bank["kernels"], samplerate)
    window_ms = 1000.0 * ctx["n_pca"] / samplerate

    print(f"\n=== alpha = {alpha:g}, ΔERB = {delta_erb:g}: {n_bands} bands, "
          f"low-band mode {args.low_band_mode}")
    widths = (f"; widths {bank['bandwidths'].min():.1f} .. "
              f"{bank['bandwidths'].max():.1f} Hz") if len(bank["bandwidths"]) else ""
    print(f"    B_min = {bank['b_min']:.1f} Hz, ERB(f) crosses it at "
          f"{bank['transition_hz']:.0f} Hz{widths}")
    if bank["low_cutoff"] is not None:
        print(f"    low band: one band up to {bank['low_cutoff']:.0f} Hz")
    if len(bank["centers"]):
        print(f"    centres: {bank['centers'][0]:.1f} Hz .. {bank['centers'][-1]:.1f} Hz")
    print(f"    kernels: {bank['nfft']} taps, 99 % energy within "
          f"{spreads.max():.1f} ms at worst, NAE window is {window_ms:.1f} ms"
          f" ({'within' if spreads.max() <= window_ms else 'LONGER THAN'} the window)")

    # Control 1, in frequency and before anything else: the masks must sum to
    # one at every bin of the design grid. This is the condition perfect
    # reconstruction rests on, it costs microseconds to check, and section 14 is
    # explicit that no PCA is to be run until the bank is known to reconstruct.
    mask_error = float(np.max(np.abs(bank["masks"].sum(axis=0) - 1.0)))
    print(f"    bank: max |sum_b W_b(f) - 1| = {mask_error:.3e}")
    if mask_error > 1.0e-12:
        raise RuntimeError(
            f"ERB bank does not reconstruct: max |sum_b W_b - 1| = {mask_error:.3e}. "
            "Refusing to run the per-band PCA on a bank that loses signal.")

    left = audio[:, 0]
    right = audio[:, 1]
    recon_l = np.zeros(num_samples)
    recon_r = np.zeros(num_samples)

    if not args.check_only:
        c1_erb = np.zeros_like(ctx["c1_ref"])
        c2_erb = np.zeros_like(ctx["c2_ref"])
        vectors_all = np.zeros((n_bands, len(ctx["pan_seq"]), 2, 2))
        values_all = np.zeros((n_bands, len(ctx["pan_seq"]), 2))

    forced = ctx["vec_ref"] if args.same_axis else None

    if args.filtering == "matrix":
        # No bank in the audio path at all: see nae_process_matrix. The
        # reference this run is compared against has to come through the same
        # overlap-add, so it is recomputed here as the one-band case rather than
        # taken from the whole-signal broadband run.
        # The broadband run of THIS configuration, first: it is both the
        # reference the figures are read against and, for the section 15
        # control, the axis to force. Taking that axis from the whole-signal run
        # instead would be wrong the moment --cov-window makes the two estimate
        # over different windows -- the control would then be measuring the
        # difference between two windows and reporting it as a failure of the
        # bank.
        flat_cov = np.ones((1, (bank["n_cov"] // 2) + 1))
        flat_syn = np.ones((1, (bank["n_syn"] // 2) + 1))
        ref_c1_full, ref_c2_full, ref_vec, _, _, _, _ = nae_process_matrix(
            left, right, ctx["pan_seq"], frame_size, steps,
            flat_cov, bank["n_cov"], flat_syn, bank["n_syn"])
        ctx["c1_ref_mode"] = ref_c1_full
        ctx["c2_ref_mode"] = ref_c2_full
        if args.same_axis:
            forced = ref_vec[0]

        (c1_erb, c2_erb, vectors_all, values_all,
         recon_l, recon_r, alias) = nae_process_matrix(
            left, right, ctx["pan_seq"], frame_size, steps,
            bank["masks_cov"], bank["n_cov"], bank["masks_syn"], bank["n_syn"],
            masks_ref=bank["masks_ref"], n_ref=bank["n_ref"],
            forced_vectors=forced, progress=True)
        n_recon = len(recon_l)
        left = left[:n_recon]
        right = right[:n_recon]
    elif args.filtering == "block":
        # The realisable path: one transform pair per window, every band from
        # it, the loops inverted. See nae_process_banded.
        c1_erb, c2_erb, vectors_all, values_all, recon_l, recon_r = nae_process_banded(
            left, right, ctx["pan_seq"], frame_size, steps,
            bank["mask_spectra"], bank["nfft"], bank["delay"],
            forced_vectors=forced, progress=True, pca=not args.check_only)
        # The block loop reconstructs only the samples the windows tile, which
        # is the analysed part of the signal; the tail beyond it never reaches
        # the PCA either.
        n_recon = len(recon_l)
        left = left[:n_recon]
        right = right[:n_recon]
    else:
        for b in range(n_bands):
            kspec = kernel_spectrum(bank["kernels"][b], ctx["fft_len"])
            band_l = filter_band(ctx["spec_l"], kspec, ctx["fft_len"],
                                 bank["delay"], num_samples)
            band_r = filter_band(ctx["spec_r"], kspec, ctx["fft_len"],
                                 bank["delay"], num_samples)
            recon_l += band_l
            recon_r += band_r
            if args.check_only:
                continue
            c1_b, c2_b, vectors, values = nae_process(
                band_l, band_r, ctx["pan_seq"], frame_size, steps, forced_vectors=forced)
            c1_erb += c1_b
            c2_erb += c2_b
            vectors_all[b] = vectors
            values_all[b] = values
            if sys.stdout.isatty():
                # Carriage return only on a terminal: piped to a file or a log,
                # a progress line that never advances a row is one line per band.
                print(f"    band {b + 1:3d}/{n_bands}", end="\r")

        if sys.stdout.isatty():
            print(" " * 60, end="\r")

    # Control 2, in time: the bands summed straight back must be the input. The
    # common delay has already been taken out of every band, so what is compared
    # is x(t) and not x(t - T0).
    peak = float(np.max(np.abs(audio)))
    recon = reconstruction_report(left, right, recon_l, recon_r, samplerate, peak)
    if args.filtering == "block":
        print("    (block mode: the bands are wrapped by the transform, but the "
              "masks still sum to one, so the sum is still exact)")
    print(f"    reconstruction: max |e| = {recon['max_abs']:.3e} "
          f"({recon['max_rel']:.2e} of peak), RMS = {recon['rms']:.3e}, "
          f"corr = {recon['corr_l']:.12f} / {recon['corr_r']:.12f}")
    if recon["max_rel"] > RECON_TOL:
        raise RuntimeError(
            f"band-sum reconstruction error {recon['max_rel']:.3e} of peak exceeds "
            f"{RECON_TOL:.0e}. Not continuing to the per-band PCA (section 14).")
    print("    reconstruction control: PASS")

    if args.analysis:
        plot_erb_bank(bank, samplerate, label, prefix)
        plot_reconstruction_error(recon, samplerate, label, prefix)
        plot_kernels(bank, spreads, window_ms, samplerate, label, prefix)

    if args.check_only:
        return None

    if args.filtering == "matrix":
        print("    NOTE: overlap-add is nae.cpp's, not the script's, so C1/C2 "
              "figures are comparable only within this mode; the axis figures "
              "(theta, q) are comparable with whole and block.")
        if alias is not None:
            err, sig = alias
            total = db(rms(err)) - db(rms(sig))
            print(f"    aliasing control (C1 left, this synthesis window against "
                  f"{bank['n_ref']}): {total:+.1f} dB overall")
            fq, ps = avg_spectrum(sig, samplerate, 4096)
            _, pe = avg_spectrum(err, samplerate, 4096)
            parts = []
            for lo, hi in ((20, 200), (200, 1000), (1000, 4000),
                           (4000, 10000), (10000, 20000)):
                sel = (fq >= lo) & (fq < hi)
                if not np.any(sel):
                    continue
                rel = (10.0 * np.log10(pe[sel].mean() + 1.0e-300)
                       - 10.0 * np.log10(ps[sel].mean() + 1.0e-300))
                parts.append(f"{lo // 1000 if lo >= 1000 else lo}"
                             f"{'k' if lo >= 1000 else ''}-"
                             f"{hi // 1000 if hi >= 1000 else hi}"
                             f"{'k' if hi >= 1000 else ''}: {rel:+.0f}")
            print("      per band (dB below signal): " + ", ".join(parts))
            if total > -40.0:
                print("      WARNING: the wrap is audible at this synthesis window; "
                      "raise --syn-window.")

    if args.filtering == "block":
        # The wrap is invisible in the sum but not between bands. This is what
        # the block structure costs, and it is the number to weigh before
        # putting the algorithm into a real-time engine.
        print("    NOTE: per-band content differs from the exact path by the "
              "circular wrap of the block; compare with --filtering whole.")

    # The structural control of section 15. With one axis for every band the
    # whole chain is linear in the mid/side signal, and the bank sums back to
    # the input, so the sum of the bands has to be the broadband run itself.
    if args.same_axis:
        ref1 = ctx["c1_ref_mode"] if args.filtering == "matrix" else ctx["c1_ref"]
        ref2 = ctx["c2_ref_mode"] if args.filtering == "matrix" else ctx["c2_ref"]
        d1 = float(np.max(np.abs(c1_erb - ref1)))
        d2 = float(np.max(np.abs(c2_erb - ref2)))
        rel = max(d1, d2) / max(peak, 1.0e-30)
        print(f"    structural control (same broadband axis): "
              f"max |C1_ERB - C1| = {d1:.3e}, max |C2_ERB - C2| = {d2:.3e} "
              f"({rel:.2e} of peak) -> {'PASS' if rel < RECON_TOL else 'FAIL'}")

    theta = axis_angles(vectors_all)
    q = degeneracy_ratio(values_all)

    skip = frame_size * (steps - 1)
    out_c1 = c1_erb[skip:]
    out_c2 = c2_erb[skip:]
    if args.filtering == "matrix":
        # This mode is compared against its own broadband run, because its
        # overlap-add is the engine's and the whole/block reference's is the
        # script's. And the engine has not emitted the last covsteps-1 frames
        # yet -- they are still in the accumulator -- so the valid region is
        # that much shorter.
        ref_c1 = ctx["c1_ref_mode"][skip:]
        ref_c2 = ctx["c2_ref_mode"][skip:]
        valid = len(out_c1) - skip
        out_c1, out_c2 = out_c1[:valid], out_c2[:valid]
        ref_c1, ref_c2 = ref_c1[:valid], ref_c2[:valid]
    else:
        ref_c1 = ctx["c1_ref"][skip:]
        ref_c2 = ctx["c2_ref"][skip:]

    sf.write(f"{prefix}_c1.wav", out_c1, samplerate)
    sf.write(f"{prefix}_c2.wav", out_c2, samplerate)
    print(f"    wrote {prefix}_c1.wav and {prefix}_c2.wav")

    if args.write_reference:
        # The broadband run of this same configuration, cut to the same region.
        # Its name carries the mode, the filtering and the analysis window but
        # NOT alpha or delta-erb, because the reference has no bank: two runs
        # that differ only in the bank share one reference, and writing it twice
        # under two names would invite comparing a file against itself.
        ref_tag = f"broadband_{args.mode}_{args.filtering}"
        if ctx["n_cov"] != ctx["n_pca"]:
            ref_tag += f"_cov{ctx['n_cov']}"
        ref_prefix = f"{ctx['base_prefix']}_{ref_tag}"
        sf.write(f"{ref_prefix}_c1.wav", ref_c1, samplerate)
        sf.write(f"{ref_prefix}_c2.wav", ref_c2, samplerate)
        print(f"    wrote {ref_prefix}_c1.wav and {ref_prefix}_c2.wav "
              f"(broadband, for A/B)")

    dtheta = np.degrees(np.abs(wrap_axis(np.diff(theta, axis=1))))
    dbtheta = (np.degrees(np.abs(wrap_axis(np.diff(theta, axis=0))))
               if n_bands > 1 else np.array([0.0]))

    row = {
        "alpha": alpha,
        "delta_erb": delta_erb,
        "low_band_mode": args.low_band_mode,
        "filtering": args.filtering,
        "nfft": bank["nfft"],
        "n_cov": bank["n_cov"],
        "n_bands": n_bands,
        "b_min_hz": bank["b_min"],
        "transition_hz": bank["transition_hz"],
        "max_kernel_ms": float(spreads.max()),
        "window_ms": window_ms,
        "rms_c1": rms(out_c1),
        "rms_c2": rms(out_c2),
        "c2_over_c1_db": db(rms(out_c2)) - db(rms(out_c1)),
        "corr_c1": correlation(out_c1, ref_c1),
        "corr_c2": correlation(out_c2, ref_c2),
        "resid_c1_db": db(rms(out_c1 - ref_c1)) - db(rms(ref_c1)),
        "resid_c2_db": db(rms(out_c2 - ref_c2)) - db(rms(ref_c2)),
        "dtheta_median": float(np.median(dtheta)),
        "dtheta_p90": float(np.percentile(dtheta, 90)),
        "dtheta_p99": float(np.percentile(dtheta, 99)),
        "dbtheta_median": float(np.median(dbtheta)),
        "recon_max_rel": recon["max_rel"],
        "recon_rms": recon["rms"],
    }
    for threshold in DEGENERACY_THRESHOLDS:
        row[f"q_lt_{threshold}"] = float(np.mean(q < threshold))

    print(f"    RMS C1 = {row['rms_c1']:.6f}  RMS C2 = {row['rms_c2']:.6f}  "
          f"C2/C1 = {row['c2_over_c1_db']:+.2f} dB")
    print(f"    vs broadband: corr C1 = {row['corr_c1']:.6f}, C2 = {row['corr_c2']:.6f}; "
          f"residual C1 = {row['resid_c1_db']:+.2f} dB, C2 = {row['resid_c2_db']:+.2f} dB")
    print(f"    axis: median |Δt theta| = {row['dtheta_median']:.2f} deg "
          f"(p90 {row['dtheta_p90']:.2f}, p99 {row['dtheta_p99']:.2f}), "
          f"median |Δb theta| = {row['dbtheta_median']:.2f} deg")
    print("    degenerate cells: " + ", ".join(
        f"q<{t}: {row[f'q_lt_{t}'] * 100.0:.1f}%" for t in DEGENERACY_THRESHOLDS))

    if args.analysis:
        plot_theta_map(theta, bank, samplerate, frame_size, label, prefix)
        plot_theta_stability(theta, bank, label, prefix)
        plot_theta_continuity(theta, bank, label, prefix)
        plot_degeneracy(q, bank, label, prefix)
        plot_vs_broadband(out_c1, out_c2, ref_c1, ref_c2, samplerate, label, prefix)

    np.savez(f"{prefix}_metrics.npz",
             alpha=alpha,
             delta_erb=delta_erb,
             b_min=bank["b_min"],
             centers=bank["centers"],
             bandwidths=bank["bandwidths"],
             low_cutoff=(np.nan if bank["low_cutoff"] is None else bank["low_cutoff"]),
             kernel_spread_ms=spreads,
             theta=theta.astype(np.float32),
             eigenvalues=values_all.astype(np.float32),
             q=q.astype(np.float32),
             frame_size=frame_size,
             covsteps=steps,
             samplerate=samplerate)
    print(f"    metrics written to {prefix}_metrics.npz")
    return row


def main():
    parser = argparse.ArgumentParser(
        description="NAE NatAmbio with a per-band PCA over an ERB-like bank "
                    "whose resolution is limited by the NAE covariance window.")
    parser.add_argument("wavfile",
                        help="Fichero WAV estéreo a analizar")
    parser.add_argument("--mode", choices=["alpha", "beta"], default=MODE,
                        help="Modo NAE: alpha (main) o beta (ambient). Por defecto: alpha")
    parser.add_argument("--analysis", type=str2bool, default=ANALYSIS,
                        metavar="true|false",
                        help="Modo análisis: genera las gráficas matplotlib (true) o solo procesa (false). Por defecto: true")
    parser.add_argument("--frame-size", type=int, default=FRAME_SIZE,
                        help=f"Tamaño de frame en muestras. Por defecto: {FRAME_SIZE}")
    parser.add_argument("--covsteps", type=int, default=COVSTEPS,
                        help=f"Número de pasos de covarianza solapados. Por defecto: {COVSTEPS}")
    parser.add_argument("--alpha", type=float, nargs="+", default=[ALPHA],
                        metavar="A",
                        help="Factor del suelo de ancho de banda, B_min = alpha*fs/N_PCA. "
                             f"Varios valores se barren. Por defecto: {ALPHA:g}")
    parser.add_argument("--delta-erb", type=float, nargs="+", default=[DELTA_ERB],
                        metavar="D",
                        help="Separación entre centros en unidades ERB, limitada por eta*B_min. "
                             f"Varios valores se barren. Por defecto: {DELTA_ERB:g}")
    parser.add_argument("--erb-bandwidth", type=float, default=ERB_BANDWIDTH,
                        metavar="B",
                        help="Ancho de cada filtro en unidades ERB antes del suelo, "
                             f"independiente de --delta-erb. Por defecto: {ERB_BANDWIDTH:g}")
    parser.add_argument("--erb-order", type=int, default=ERB_ORDER,
                        help=f"Orden del gammatone. Por defecto: {ERB_ORDER}")
    parser.add_argument("--erb-fmin", type=float, default=ERB_FMIN,
                        help=f"Frecuencia del primer centro (Hz). Por defecto: {ERB_FMIN:g}")
    parser.add_argument("--erb-fmax", type=float, default=ERB_FMAX,
                        help=f"Frecuencia del último centro (Hz). Por defecto: {ERB_FMAX:g}")
    parser.add_argument("--eta", type=float, default=ETA,
                        help="Suelo de separación entre centros, en unidades de B_min. "
                             f"Por defecto: {ETA:g}")
    parser.add_argument("--nfft", type=int, default=NFFT_DESIGN,
                        help="Bins de la rejilla de diseño del banco, y taps del FIR de "
                             f"cada banda. Por defecto: {NFFT_DESIGN}")
    parser.add_argument("--low-band-mode", choices=list(LOW_BAND_MODES),
                        default="continuous",
                        help="Región grave: bandas continuas o una sola banda ancha "
                             "hasta 250 / 400 Hz. Por defecto: continuous")
    parser.add_argument("--filtering", choices=["whole", "block", "matrix"],
                        default="whole",
                        help="whole: filtrado exacto de la señal entera con el FIR de "
                             "cada banda (referencia, no realizable). block: el bucle "
                             "por bloques de la sección 13, una ventana cada vez. "
                             "matrix: sin banco en el camino de audio -- covarianzas por "
                             "Parseval y una matriz 2x2 dependiente de la frecuencia, "
                             "FFT sobre la ventana de covarianza sin zero-padding, con "
                             "el overlap-add de nae.cpp; latencia añadida cero. "
                             "Por defecto: whole")
    parser.add_argument("--cov-window", type=int, default=0, metavar="N",
                        help="Muestras de la ventana de covarianza, hacia el pasado desde "
                             "el mismo final. Solo con --filtering matrix. Alarga la "
                             "estimación del eje sin tocar la reconstrucción, así que no "
                             "añade latencia. Múltiplo de --frame-size y >= "
                             "frame_size*covsteps. Por defecto: 0 = la ventana de "
                             "reconstrucción")
    parser.add_argument("--syn-window", type=int, default=0, metavar="N",
                        help="Muestras de la transformada de síntesis, hacia el pasado "
                             "desde el mismo final. Solo con --filtering matrix. Es la "
                             "holgura que evita que la convolución circular se pliegue "
                             "sobre la trama emitida; no cambia la latencia. "
                             "Por defecto: 0 = 2 x la ventana de reconstrucción")
    parser.add_argument("--write-reference", type=str2bool, default=False,
                        metavar="true|false",
                        help="Escribe también el C1/C2 del NAE broadband de ESTA misma "
                             "configuración (mismo modo, mismo overlap-add, mismo "
                             "tratamiento de beta), que es contra lo que hay que "
                             "comparar de oído para aislar el efecto de las bandas. "
                             "Por defecto: false")
    parser.add_argument("--same-axis", type=str2bool, default=False,
                        metavar="true|false",
                        help="Control estructural de la sección 15: todas las bandas usan "
                             "el autovector broadband. Debe reproducir el NAE broadband.")
    parser.add_argument("--check-only", type=str2bool, default=False,
                        metavar="true|false",
                        help="Solo el control de reconstrucción de la sección 14: descompone "
                             "en bandas, suma y mide, sin PCA.")
    args = parser.parse_args()

    frame_size = args.frame_size
    steps = args.covsteps
    mode_amb = (args.mode == "beta")
    base_prefix = args.wavfile.rsplit(".")[0]

    if args.filtering != "matrix" and args.nfft & (args.nfft - 1):
        raise ValueError(f"--nfft must be a power of two, got {args.nfft}.")

    audio, samplerate = sf.read(args.wavfile)
    if audio.ndim != 2 or audio.shape[1] != 2:
        raise ValueError("Input must be a stereo audio signal.")
    audio = np.asarray(audio, dtype=float)
    num_samples = audio.shape[0]
    end = int(np.floor(num_samples / frame_size)) * frame_size
    if end < frame_size * steps:
        raise ValueError("Input is shorter than one covariance window.")

    # The window everything else is conditioned by (section 1). Printed in full
    # because every figure below follows from it: change the frame size or the
    # covsteps and the bank changes with them, which is the point.
    n_pca = frame_size * steps
    window_ms = 1000.0 * n_pca / samplerate
    df_data = samplerate / float(n_pca)

    # The analysis window: the same end, further back. Only matrix mode can use
    # it -- whole and block estimate the axis inside nae_process, whose window is
    # the covsteps buffer by construction, so accepting the flag there would
    # change B_min and the bank while silently leaving the estimate alone.
    n_cov = n_pca
    if args.cov_window:
        if args.filtering != "matrix":
            raise ValueError("--cov-window only applies to --filtering matrix; "
                             "whole and block estimate the axis over the covsteps "
                             "window by construction.")
        if args.cov_window % frame_size:
            n_cov = int(np.ceil(args.cov_window / frame_size)) * frame_size
            print(f"NOTE: --cov-window {args.cov_window} is not a multiple of "
                  f"--frame-size {frame_size}; rounded up to {n_cov}.")
        else:
            n_cov = args.cov_window
        if n_cov < n_pca:
            raise ValueError(f"--cov-window {n_cov} is shorter than the "
                             f"reconstruction window ({n_pca}); the axis cannot be "
                             "estimated over less than it is applied to.")

    n_syn = None
    if args.filtering == "matrix":
        n_syn = args.syn_window if args.syn_window else 2 * n_pca
        if n_syn % frame_size:
            n_syn = int(np.ceil(n_syn / frame_size)) * frame_size
            print(f"NOTE: --syn-window rounded up to {n_syn} (multiple of "
                  f"--frame-size {frame_size}).")
        if n_syn < n_pca:
            raise ValueError(f"--syn-window {n_syn} is shorter than the reconstruction "
                             f"window ({n_pca}); there would be no headroom at all.")
        print(f"Synthesis window: {n_syn} samples "
              f"({1000.0 * n_syn / samplerate:.1f} ms), {n_syn - n_pca} samples of "
              f"headroom against the circular wrap. Latency unchanged.")
    elif args.syn_window:
        raise ValueError("--syn-window only applies to --filtering matrix.")

    if args.filtering == "matrix":
        # The masks live on the data's own grid: no zero-padding, because the
        # product is applied to this very transform and a finer grid would only
        # interpolate a mask that is never evaluated anywhere else. It also
        # makes B_min/df exactly alpha -- alpha IS the number of bins the
        # narrowest band spans.
        if args.nfft != n_pca:
            print(f"NOTE: --filtering matrix designs the bank on the covariance "
                  f"window itself; --nfft {args.nfft} ignored, using {n_pca}.")
        args.nfft = n_pca

    print(f"Successfully loaded stereo WAV file: {args.wavfile}")
    print(f"Sample rate: {samplerate} Hz")
    print(f"Audio samples: {num_samples}")
    print(f"Mode: {args.mode} | Analysis: {args.analysis}")
    print(f"NAE window: frame_size {frame_size} x covsteps {steps} = {n_pca} samples "
          f"({window_ms:.1f} ms), fs/N = {df_data:.1f} Hz")
    if n_cov != n_pca:
        print(f"Analysis window: {n_cov} samples "
              f"({1000.0 * n_cov / samplerate:.1f} ms, {n_cov // frame_size} periods "
              f"back), fs/N_cov = {samplerate / n_cov:.1f} Hz. Reconstruction and "
              f"latency unchanged.")
        print(f"    (to keep the same bank as cov_window={n_pca}, scale alpha by "
              f"{n_cov / n_pca:g})")
    print(f"Bank design grid: {args.nfft} bins "
          f"({samplerate / args.nfft:.2f} Hz), kernels {args.nfft} taps, "
          f"common delay T0 = {args.nfft // 2} samples")
    # B_min follows the analysis window, which --cov-window may have lengthened.
    df_cov = samplerate / float(n_cov)
    print(f"alpha values: {', '.join(f'{a:g}' for a in args.alpha)}  ->  B_min = "
          + ", ".join(f"{a * df_cov:.1f} Hz" for a in args.alpha))
    print(f"ΔERB values: {', '.join(f'{d:g}' for d in args.delta_erb)}")
    print(f"Low-band mode: {args.low_band_mode}")

    # A centre above Nyquist is a band with nothing in it: the mask is evaluated
    # only up to samplerate/2, so such a band gets the tail of its own skirt and
    # the pointwise normalisation hands its share to the neighbours. The bank
    # still reconstructs, but the top of the range stops meaning what it says,
    # so it is clamped and the change is reported rather than left to be found
    # in a plot.
    nyquist = samplerate / 2.0
    if args.erb_fmax > 0.95 * nyquist:
        print(f"NOTE: --erb-fmax {args.erb_fmax:g} Hz is above 0.95 x Nyquist "
              f"({nyquist:.0f} Hz); clamped to {0.95 * nyquist:.0f} Hz.")
        args.erb_fmax = 0.95 * nyquist

    pan_seq = pan_sequence(audio, frame_size, end, mode_amb)
    if not np.all(np.isfinite(pan_seq)):
        # A window with a digitally silent channel makes np.corrcoef return nan,
        # which would then poison every band. Same expression as the broadband
        # script, so the same hazard: report it rather than change the formula.
        n_bad = int(np.sum(~np.isfinite(pan_seq)))
        print(f"WARNING: pan is not finite in {n_bad} window(s) "
              "(silent channel in the correlation window); results will contain NaN.")

    # The broadband run: the reference of section 20, the axis of the section 15
    # control, and the thing the whole experiment is a variation on. It is
    # always run, and it is `nae_process` -- the same function each band goes
    # through -- so that the two are comparable by construction.
    print("\n=== Broadband NAE reference")
    c1_ref, c2_ref, vec_ref, val_ref = nae_process(
        audio[:, 0], audio[:, 1], pan_seq, frame_size, steps)
    q_ref = val_ref[:, 0] / (val_ref[:, 1] + 1.0e-20)
    print(f"    {len(pan_seq)} windows, RMS C1 = {rms(c1_ref[frame_size * (steps - 1):]):.6f}, "
          f"RMS C2 = {rms(c2_ref[frame_size * (steps - 1):]):.6f}, "
          f"median lambda1/lambda2 = {np.median(q_ref):.2f}")

    if args.filtering == "whole":
        spec_l, spec_r, fft_len = signal_spectra(audio[:, 0], audio[:, 1], args.nfft)
        print(f"    filtering FFT length: {fft_len} bins ({fft_len / samplerate:.1f} s)")
    else:
        # block and matrix transform one window at a time; the whole-signal
        # spectrum would be hundreds of megabytes nothing reads.
        spec_l = spec_r = None
        fft_len = 0

    ctx = {
        "audio": audio, "samplerate": samplerate, "num_samples": num_samples,
        "frame_size": frame_size, "steps": steps, "args": args, "n_pca": n_pca,
        "n_cov": n_cov, "n_syn": n_syn,
        "pan_seq": pan_seq, "c1_ref": c1_ref, "c2_ref": c2_ref, "vec_ref": vec_ref,
        "spec_l": spec_l, "spec_r": spec_r, "fft_len": fft_len,
        "base_prefix": base_prefix,
    }

    rows = []
    for alpha in args.alpha:
        for delta_erb in args.delta_erb:
            row = run_bank(delta_erb, alpha, ctx)
            if row is not None:
                rows.append(row)

    if not rows:
        return

    # The table of sections 19 and 20. Printed and written: the point of a sweep
    # is to be read side by side, and the point of the CSV is that the next
    # sweep can be put next to this one.
    fields = ["alpha", "delta_erb", "low_band_mode", "filtering", "nfft", "n_cov",
              "n_bands",
              "b_min_hz", "transition_hz", "max_kernel_ms", "window_ms",
              "rms_c1", "rms_c2", "c2_over_c1_db", "corr_c1", "corr_c2",
              "resid_c1_db", "resid_c2_db",
              "dtheta_median", "dtheta_p90", "dtheta_p99", "dbtheta_median"]
    fields += [f"q_lt_{t}" for t in DEGENERACY_THRESHOLDS]
    fields += ["recon_max_rel", "recon_rms"]

    print("\n=== Sweep")
    header = ["alpha", "ΔERB", "bands", "B_min", "cross", "ker ms", "C2/C1 dB",
              "corr C1", "corr C2", "res C2 dB", "Δt med", "Δb med"] + \
             [f"q<{t} %" for t in DEGENERACY_THRESHOLDS]
    print("\t".join(header))
    for r in rows:
        line = [f"{r['alpha']:g}", f"{r['delta_erb']:g}", f"{r['n_bands']}",
                f"{r['b_min_hz']:.1f}", f"{r['transition_hz']:.0f}",
                f"{r['max_kernel_ms']:.1f}", f"{r['c2_over_c1_db']:+.2f}",
                f"{r['corr_c1']:.4f}", f"{r['corr_c2']:.4f}",
                f"{r['resid_c2_db']:+.2f}",
                f"{r['dtheta_median']:.2f}", f"{r['dbtheta_median']:.2f}"]
        line += [f"{r[f'q_lt_{t}'] * 100.0:.1f}" for t in DEGENERACY_THRESHOLDS]
        print("\t".join(line))

    csv_path = f"{base_prefix}_erb_sweep.csv"
    with open(csv_path, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for r in rows:
            writer.writerow({k: r[k] for k in fields})
    print(f"\nSweep table written to {csv_path}")

    if args.analysis and len(rows) > 1:
        sweep_label = (f"{os.path.basename(args.wavfile)} — NAE ERB {args.mode}, "
                       f"N_PCA = {n_pca} ({window_ms:.1f} ms)")
        plot_sweep(rows, sweep_label, base_prefix)


if __name__ == "__main__":
    main()
