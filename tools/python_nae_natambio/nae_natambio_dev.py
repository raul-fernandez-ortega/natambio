# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
nae_natambio_dev.py — NAE (NatAmbio Ambient Extraction), offline Python
version, DEVELOPMENT build.

This is the `_dev` variant of nae_natambio.py: it keeps the NAE algorithm and
every analysis plot of the stable script byte-for-byte, and adds one extra
study (see "C1/C2 lag-correlation study" below).

Implements the SAME NAE algorithm as the other two incarnations of the project:

  * tools/ladspa_nae_natambio  — LADSPA plugin in C (real-time)
  * src/nae.cpp                — NAE engine of the JACK client `natambio` (real-time)

Unlike those two (which process in real time inside an audio host), this script
works **offline on a stereo WAV file**: it decomposes the signal via PCA
(eigenvalues/eigenvectors of the 2x2 covariance matrix over the mid/side
components, with an overlapping window of `covsteps` frames) into two components
— main and ambient — and writes them as `<input>_c1.wav` and `<input>_c2.wav`.

It serves two purposes:

  1. Run the NAE algorithm on a WAV in a reproducible way without a real-time
     audio server.
  2. Analyse the process: with `--analysis true` it generates matplotlib plots
     (L/R correlation and its histogram, eigenvector rotation, eigenvalue ratio,
     C1/C2 component levels, and mid/side scatter with eigenvectors overlaid).
     Each title includes the WAV name and the mode.

C1/C2 lag-correlation study (new in this _dev version)
------------------------------------------------------
For every analysis frame, C1 is delayed by a set of lags (0..`--lag-max`
samples in `--lag-step` steps, i.e. 0, 50, ... 1000 by default) and the
Pearson correlation of the delayed C1' against C2 is computed over that frame.
The correlation window spans `frame_size*covsteps` samples (the same extent as
the PCA covariance window) and advances one `frame_size` per frame, so
consecutive windows overlap exactly as the PCA's do; `--lag-window` overrides
that size. The two quantities kept per frame are:

  * the lag that maximised the correlation at that instant, and
  * the value of that maximum correlation.

Left and right are studied **separately** -- two lags and two correlations,
never mixed. C2 is the side-like component, so its two channels are largely
antiphase and averaging their correlations would cancel exactly the content
this study looks for. Each channel gets its own stacked two-panel figure (lag
on top, correlation below, shared time axis), saved as
`<input>_lag_correlation_left.png` and `<input>_lag_correlation_right.png`.
They answer the question "does the ambient component look like a delayed copy
of the main one, and with how much delay?" over the course of the recording.

The lag is picked by |correlation| by default (`--lag-abs`): np.linalg.eig
returns eigenvectors with an arbitrary sign on each frame, so the polarity of
the C1/C2 reconstruction flips from time to time and a signed maximum silently
misses those windows.

Projection of the delayed C1 onto C2 (`--projection`, on by default)
--------------------------------------------------------------------
Reusing the per-window lag of the study above, each window is fitted by least
squares (see `project_delayed_c1_on_c2`), splitting C2 into the part linearly
explainable from the delayed C1 and the rest. Two extra WAVs are written next
to `<input>_c1.wav` / `<input>_c2.wav`, sample-aligned with them:

  * `<input>_c1_delayed_projection_on_c2.wav` — the projection P = a*C1', in
    C2's timebase (i.e. still delayed)
  * `<input>_c2_residual.wav`                 — the residual E = C2 - P
  * `<input>_projection_undelayed.wav`        — the same projection with the
    delay taken back out, so it lines up with C1
  * `<input>_c1_minus_projection.wav`         — C1 minus the undelayed
    projection

The last one is the point of the exercise: whatever of C1 came back later in
C2 as a delayed copy is subtracted from C1 in time alignment, leaving the main
component without the part that feeds the ambience. The delay is undone per
window, inside the overlap-add, because tau changes from window to window --
shifting the finished signal by one value would be wrong everywhere tau moved.

`<input>_c2.wav` is the C2_original of the specification, so it is not written
twice.

Because it relies only on numpy, soundfile and matplotlib (all cross-platform),
it runs on **GNU/Linux** and **MS Windows** without modification.

Usage:
    python nae_natambio_dev.py <file.wav> [--mode alpha|beta]
                                          [--analysis true|false]
                                          [--frame-size N] [--covsteps N]
                                          [--lag-max N] [--lag-step N]
                                          [--lag-window N] [--lag-abs true|false]
                                          [--projection true|false]

This directory (tools/python_nae_natambio) is a self-contained Python project:
it shares no code with the rest of the repository.
"""

import os
import sys
import argparse
import random
import soundfile as sf
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter

# Scale the font size of every plot by 1.25. The other text sizes (titles,
# labels, ticks, legend) default to relative values, so scaling the base
# font.size scales them all by the same x1.25 factor.
plt.rcParams.update({"font.size": plt.rcParams["font.size"] * 1.25})

# Figure size for every plot (shown and saved as PNG): 16:9 aspect ratio,
# scaled by FIG_SCALE for larger output. Base 16:9 is (8, 4.5); x2 -> (16, 9).
FIG_SCALE = 2
FIGSIZE = (8 * FIG_SCALE, 4.5 * FIG_SCALE)

ANALYSIS = True
COVSTEPS = 5
ICORRL = 20
NATAMBCOEFF = -2.5
MODE = "alpha"   # default NAE mode: alpha (main) / beta (ambient)

# C1/C2 lag-correlation study: C1 is delayed by 0, LAG_STEP, 2*LAG_STEP ...
# up to LAG_MAX samples and each delayed C1' is correlated against C2.
LAG_MAX = 1000
LAG_STEP = 50
# Write the least-squares projection of the delayed C1 onto C2, plus the
# residual of C2, as extra WAVs next to <input>_c1.wav / <input>_c2.wav.
PROJECTION = True
# Maximise |corr| (True) or the signed correlation (False). |corr| is the
# default because np.linalg.eig returns eigenvectors with an arbitrary sign
# on each frame, so the polarity of the C1/C2 reconstruction flips from time
# to time and a signed maximum silently misses those windows.
LAG_ABS = True


def str2bool(value):
    """Parse an inline 'true'/'false' argument into a bool."""
    if isinstance(value, bool):
        return value
    if str(value).lower() in ("true", "1", "yes", "y", "t", "on"):
        return True
    if str(value).lower() in ("false", "0", "no", "n", "f", "off"):
        return False
    raise argparse.ArgumentTypeError(f"Expected true/false, got '{value}'")


def mode_label(wavfile, mode):
    """Build a title suffix with the WAV name and the NAE mode (alpha/beta)."""
    wavname = os.path.basename(wavfile)
    return f"{wavname} — NAE {mode}"


def save_fig(prefix, suffix):
    """Save the current figure as <prefix>_<suffix>.png (next to the WAV).

    `prefix` is the WAV path without its extension; `suffix` is a short
    descriptive tag for the plot. Does nothing if prefix is None.
    """
    if prefix is not None:
        plt.savefig(f"{prefix}_{suffix}.png", dpi=150, bbox_inches="tight")


def plot_correlation(correlation, samplerate, frame_size, label, prefix=None):
    time_axis = np.arange(len(correlation)) * frame_size / samplerate
    plt.figure(figsize=FIGSIZE)
    plt.plot(time_axis, correlation, linewidth=0.5)
    plt.xlabel("Time (seconds)")
    plt.ylabel("Correlation L/R")
    plt.title(f"Correlation over time. Frame size={frame_size}\n{label}")
    plt.grid(True)
    plt.xlim(0, time_axis[-1])
    plt.tight_layout()
    save_fig(prefix, "correlation")
    plt.show()
    plt.close('all')

def plot_correlation_histogram(correlation, frame_size, label, prefix=None):
    xbins = np.arange(-1, 1, 0.05)
    plt.figure(figsize=FIGSIZE)
    plt.hist(correlation, weights=np.ones(len(correlation)) / len(correlation), bins=xbins)
    plt.gca().yaxis.set_major_formatter(PercentFormatter(1))
    plt.xlabel("Correlation L/R")
    plt.title(f"Correlation over time ( Frame_size={frame_size}. Histogram )\n{label}")
    plt.grid(True)
    plt.tight_layout()
    save_fig(prefix, "correlation_histogram")
    plt.show()
    plt.close('all')

def plot_eigenvectors_rotation(angle_list, samplerate, frame_size, label, prefix=None):
    # Plot tan of eigenvectors rotation
    plt.figure(figsize=FIGSIZE)
    angletime = np.arange(0,len(angle_list))*frame_size/samplerate
    plt.plot(angletime,np.arctan(np.asarray(angle_list))/np.pi*180, linewidth=0.5)
    plt.ylabel("Angle")
    plt.xlabel("Time (s)")
    plt.xlim(0,angletime[-1])
    plt.grid()
    plt.title(f"Eigenvectors rotation\n{label}")
    save_fig(prefix, "eigenvectors_rotation")
    plt.show()
    plt.close('all')

def plot_eigenvalues(eigenvalue_list, samplerate, frame_size, label, prefix=None):
    # Plot eigenvalues
    plt.figure(figsize=FIGSIZE)
    eigentime = np.arange(0,len(eigenvalue_list[0]))*frame_size/samplerate
    plt.plot(eigentime, eigenvalue_list[0], label='Eigenvalue 1', linewidth=0.5)
    plt.plot(eigentime, eigenvalue_list[1], label='Eignevalue 2', linewidth=0.5)
    plt.plot(eigentime, np.asarray(eigenvalue_list[0])/np.asarray(eigenvalue_list[1]), label='ratio', linewidth=0.5)
    plt.semilogy()
    plt.xlim(0,eigentime[-1])
    plt.xlabel("Time (s)")
    plt.grid()
    plt.title(f"Eigenvalues ratio\n{label}")
    plt.legend()
    save_fig(prefix, "eigenvalues")
    plt.show()
    plt.close('all')


def plot_component_levels(l_c1_level,r_c1_level, l_c2_level, r_c2_level, samplerate, frame_size, label, prefix=None):
    # Plot c1 and c2 levels, left and right channels
    plt.figure(figsize=FIGSIZE)
    leveltime = np.arange(0,len(l_c1_level))*frame_size/samplerate
    plt.plot(leveltime, (np.asarray(l_c1_level) + np.asarray(r_c1_level))/2, label="C1 left + right", linewidth=0.5)
    plt.plot(leveltime, (np.asarray(l_c2_level) + np.asarray(r_c2_level))/2, label="C2 left + right", linewidth=0.5)
    plt.plot(leveltime, (np.asarray(l_c1_level) + np.asarray(r_c1_level) - np.asarray(l_c2_level) - np.asarray(r_c2_level))/2, label="C1/C2 level difference", linewidth=0.5)
    plt.ylabel("dB")
    plt.xlim(0,leveltime[-1])
    plt.xlabel("Time (s)")
    plt.grid()
    plt.legend()
    plt.title(f"C1 and C2 levels\n{label}")
    save_fig(prefix, "levels_c1c2")
    plt.show()
    plt.close('all')

    # Plot the four left/right levels of C1 and C2
    plt.figure(figsize=FIGSIZE)
    plt.plot(leveltime, np.asarray(l_c1_level), label="C1 left", linewidth=0.5)
    plt.plot(leveltime, np.asarray(r_c1_level), label="C1 right", linewidth=0.5)
    plt.plot(leveltime, np.asarray(l_c2_level), label="C2 left", linewidth=0.5)
    plt.plot(leveltime, np.asarray(r_c2_level), label="C2 right", linewidth=0.5)
    plt.ylabel("dB")
    plt.xlim(0,leveltime[-1])
    # Tighten the y-axis: dB levels collapse towards the noise floor during
    # silences, which otherwise stretches the axis. Clip the lower bound to the
    # 2nd percentile so the visible range tracks the actual signal levels.
    all_levels = np.concatenate([l_c1_level, r_c1_level, l_c2_level, r_c2_level])
    ymin = np.percentile(all_levels, 2)
    ymax = np.max(all_levels)
    margin = 0.05 * (ymax - ymin) if ymax > ymin else 1.0
    plt.ylim(ymin - margin, ymax + margin)
    plt.xlabel("Time (s)")
    plt.grid()
    plt.legend()
    plt.title(f"C1 and C2 left/right levels\n{label}")
    save_fig(prefix, "levels_lr")
    plt.show()
    plt.close('all')

    # Plot the L/R differences (dB, since levels are already in dB) of C1 and C2.
    c1_lr_diff = np.asarray(l_c1_level) - np.asarray(r_c1_level)
    c2_lr_diff = np.asarray(l_c2_level) - np.asarray(r_c2_level)
    plt.figure(figsize=FIGSIZE)
    plt.plot(leveltime, c1_lr_diff, label="C1 L-R difference (dB)", linewidth=0.5)
    plt.plot(leveltime, c2_lr_diff, label="C2 L-R difference (dB)", linewidth=0.5)
    plt.ylabel("dB")
    plt.xlim(0,leveltime[-1])
    plt.xlabel("Time (s)")
    plt.grid()
    plt.legend()
    plt.title(f"C1 and C2 L-R level differences\n{label}")
    save_fig(prefix, "lr_differences")
    plt.show()
    plt.close('all')

    # Plot the inter-component differences per side (C1 vs C2, left and right).
    plt.figure(figsize=FIGSIZE)
    plt.plot(leveltime, np.asarray(l_c1_level) - np.asarray(l_c2_level), label="left C1-C2 difference (dB)", linewidth=0.5)
    plt.plot(leveltime, np.asarray(r_c1_level) - np.asarray(r_c2_level), label="right C1-C2 difference (dB)", linewidth=0.5)
    plt.ylabel("dB")
    plt.xlim(0,leveltime[-1])
    plt.xlabel("Time (s)")
    plt.grid()
    plt.legend()
    plt.title(f"C1 vs C2 per-side level differences\n{label}")
    save_fig(prefix, "c1c2_side_differences")
    plt.show()
    plt.close('all')

def windowed_correlation(x, y, window, starts):
    """Pearson correlation of two 1-D signals over sliding windows.

    One coefficient is returned per start index in `starts`, computed over the
    `window` samples beginning at that index. The windows overlap whenever the
    spacing of `starts` is smaller than `window`, which is exactly the case
    here: the study mirrors the PCA, whose covariance window spans
    `frame_size*steps` samples and advances one `frame_size` per frame.

    The sums are obtained from prefix sums, so cost and memory stay O(len(x))
    regardless of the window size (materialising every window would blow up on
    long files). Windows where either signal is constant (zero variance, e.g.
    digital silence) yield 0.0 instead of NaN.
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    # Remove the global mean before accumulating: the window sums are
    # differences of prefix sums, and centred data keeps that subtraction well
    # conditioned. Correlation is invariant to a constant offset.
    x = x - x.mean()
    y = y - y.mean()

    px = np.concatenate(([0.0], np.cumsum(x)))
    py = np.concatenate(([0.0], np.cumsum(y)))
    pxx = np.concatenate(([0.0], np.cumsum(x * x)))
    pyy = np.concatenate(([0.0], np.cumsum(y * y)))
    pxy = np.concatenate(([0.0], np.cumsum(x * y)))

    a = np.asarray(starts, dtype=int)
    b = a + window
    sx = px[b] - px[a]
    sy = py[b] - py[a]
    sxx = pxx[b] - pxx[a]
    syy = pyy[b] - pyy[a]
    sxy = pxy[b] - pxy[a]

    num = sxy - sx * sy / window
    varx = np.maximum(sxx - sx * sx / window, 0.0)
    vary = np.maximum(syy - sy * sy / window, 0.0)
    den = np.sqrt(varx * vary)
    corr = np.zeros(len(a))
    np.divide(num, den, out=corr, where=den > 0)
    # Guard against a hair over +-1 from rounding in the prefix sums.
    return np.clip(corr, -1.0, 1.0)


def lag_correlation_study(c1, c2, frame_size, lags, channel, window=None,
                          hop=None, use_abs=LAG_ABS):
    """Correlate a delayed C1 against C2 on ONE channel, window by window.

    For each lag `d` in `lags`, C1 is delayed by `d` samples (C1'[n] = C1[n-d],
    zero-filled before the start of the signal) and correlated with C2 over
    every analysis window. `channel` is the column of the stereo pair to use:
    0 for left, 1 for right. Left and right are studied independently and never
    mixed: C2 is the side-like component, so its two channels are largely
    antiphase and any average of the two correlations would cancel.

    `window` defaults to `frame_size` and `hop` (the spacing between
    consecutive windows) to `frame_size`; the caller passes
    `window=frame_size*steps` to match the span of the PCA covariance window,
    which makes consecutive windows overlap exactly as the PCA does.

    Returns `(starts, best_lag, best_corr, corr_map)`:

      * `starts[i]`    — first sample of window i (its position on the time axis)
      * `best_lag[i]`  — lag (in samples) that maximises the correlation on window i
      * `best_corr[i]` — the correlation attained at that lag (always signed)
      * `corr_map`     — the full (len(lags), len(starts)) correlation matrix

    With `use_abs` the lag is chosen by |correlation| instead of by the signed
    value, which also picks up strongly anti-correlated (polarity-flipped) lags.
    """
    lags = np.asarray(lags, dtype=int)
    max_lag = int(lags.max())
    window = int(window) if window else frame_size
    hop = int(hop) if hop else frame_size

    n = min(len(c1), len(c2))
    if n < window:
        raise ValueError(f"Signal too short ({n} samples) for a {window}-sample "
                         f"correlation window")
    # Windows look forward from their start, as the PCA does, so the last ones
    # that would run past the end of the signal are dropped.
    starts = np.arange(0, n - window + 1, hop)

    # Front-pad C1 with `max_lag` zeros so that every lag is a plain slice.
    padded = np.concatenate((np.zeros((max_lag, c1.shape[1])), c1[:n]))

    corr_map = np.zeros((len(lags), len(starts)))
    for i, lag in enumerate(lags):
        delayed = padded[max_lag - lag:max_lag - lag + n]
        corr_map[i] = windowed_correlation(delayed[:, channel], c2[:n, channel],
                                           window, starts)

    best_idx = np.argmax(np.abs(corr_map) if use_abs else corr_map, axis=0)
    win_idx = np.arange(len(starts))
    best_lag = lags[best_idx]
    best_corr = corr_map[best_idx, win_idx]
    return starts, best_lag, best_corr, corr_map


def project_delayed_c1_on_c2(c1, c2, starts, taus, window, channel):
    """Least-squares projection of the delayed C1 onto C2, window by window.

    Implements ~/Descargas/proyeccion_C1_retardada_sobre_C2.md on one channel.
    For every analysis window, with tau the lag selected for that window
    (the one maximising the C1'/C2 correlation):

        x[n] = C1[n - tau]                        (delayed C1)
        y[n] = C2[n]
        xc, yc                                    (both centred in the window)
        a    = sum(xc*yc) / sum(xc*xc)            (least squares, sign kept)
        P    = a * xc                             (part of C2 explained by C1')
        E    = yc - P                             (residual of C2)

    Windows overlap (hop < window), so P and E are stitched back together by
    weighted overlap-add with a periodic Hann window, dividing at the end by
    the accumulated weights. That keeps the two signals continuous across
    window boundaries, makes the normalisation exact at the edges (where fewer
    windows overlap) and preserves P + E = C2 (bar the per-window mean that the
    centring removes).

    A third signal is built at the same time: the SAME projection with the
    delay taken back out, i.e. each window's P laid down at [start-tau,
    stop-tau) instead of [start, stop). That is exactly the stretch of C1 the
    block was derived from, so the result is time-aligned with C1 and can be
    summed onto it. The delay has to be undone window by window, during the
    overlap-add, because tau changes from window to window: shifting the
    finished signal by a single value would be wrong everywhere tau moved. The
    shifted overlap-add gets its own weight accumulator, since the windows no
    longer land on the same grid.

    Returns `(projection, residual, aligned, coeffs)`: the projection in C2's
    timebase, the residual of C2, the projection realigned with C1, and the
    per-window coefficient a.
    """
    n = min(len(c1), len(c2))
    taus = np.asarray(taus, dtype=int)
    max_lag = int(taus.max()) if len(taus) else 0
    # Front-pad C1 with `max_lag` zeros so that every lag is a plain slice.
    padded = np.concatenate((np.zeros(max_lag), c1[:n, channel]))
    y_all = c2[:n, channel]

    # Periodic Hann: consecutive windows sum to a constant for a hop that
    # divides the window, and the explicit weight accumulator handles the rest.
    w = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(window) / window)

    projection = np.zeros(n)
    residual = np.zeros(n)
    weight = np.zeros(n)
    # The de-delayed copy can reach up to `max_lag` samples before the start of
    # the signal, so it is accumulated in a buffer with that much head room.
    aligned = np.zeros(n + max_lag)
    aligned_weight = np.zeros(n + max_lag)
    coeffs = np.zeros(len(starts))

    for i, (start, tau) in enumerate(zip(starts, taus)):
        stop = start + window
        x = padded[start + max_lag - tau:stop + max_lag - tau]
        y = y_all[start:stop]
        xc = x - x.mean()
        yc = y - y.mean()
        xx = np.dot(xc, xc)
        a = np.dot(xc, yc) / xx if xx > 0 else 0.0
        coeffs[i] = a
        p = a * xc
        projection[start:stop] += w * p
        residual[start:stop] += w * (yc - p)
        weight[start:stop] += w
        # Same block, delay removed: it lands on the C1 samples it came from.
        aligned[start - tau + max_lag:stop - tau + max_lag] += w * p
        aligned_weight[start - tau + max_lag:stop - tau + max_lag] += w

    covered = weight > 1e-12
    projection[covered] /= weight[covered]
    residual[covered] /= weight[covered]
    covered = aligned_weight > 1e-12
    aligned[covered] /= aligned_weight[covered]
    return projection, residual, aligned[max_lag:max_lag + n], coeffs


def plot_lag_correlation(starts, best_lag, best_corr, samplerate, window, lags,
                         channel_name, use_abs, label, prefix=None):
    """Two stacked panels for one channel: winning C1 delay on top, correlation below."""
    time_axis = np.asarray(starts) / samplerate
    lag_step = int(lags[1] - lags[0]) if len(lags) > 1 else 1
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=FIGSIZE, sharex=True)

    # A thin line is unreadable here: the lag is a staircase that often sits
    # still on one value, and a 0.5 pt trace lands right on top of a grid line.
    ax1.plot(time_axis, best_lag, linewidth=1.2, drawstyle="steps-post")
    ax1.set_ylabel("Lag of max. correlation (samples)")
    ax1.set_ylim(int(lags.min()) - lag_step, int(lags.max()) + lag_step)
    # Tick on the lags actually swept, as long as they stay readable.
    if len(lags) <= 25:
        ax1.set_yticks(lags)
    ax1.grid(True)
    criterion = "|correlation|" if use_abs else "correlation"
    ax1.set_title(f"{channel_name} channel: C1 delay maximising the C1'/C2 {criterion}. "
                  f"Lags {lags.min()}..{lags.max()} step {lag_step}, "
                  f"window={window}\n{label}")

    # Same lag expressed in milliseconds, on the right-hand axis.
    ax1r = ax1.twinx()
    ax1r.set_ylim(ax1.get_ylim()[0] * 1000.0 / samplerate,
                  ax1.get_ylim()[1] * 1000.0 / samplerate)
    ax1r.set_ylabel("Lag (ms)")

    ax2.plot(time_axis, best_corr, linewidth=0.5)
    ax2.axhline(0.0, color="black", linewidth=0.5)
    ax2.set_ylabel(f"Max. correlation C1'/C2 ({channel_name.lower()})")
    ax2.set_xlabel("Time (seconds)")
    ax2.grid(True)
    # C1 and C2 are near-orthogonal by construction, so the correlations often
    # live in a narrow band around zero: autoscale to the data (always keeping 0
    # in view) instead of pinning the axis to the full [-1, 1] range.
    cmin = min(0.0, float(np.min(best_corr)))
    cmax = max(0.0, float(np.max(best_corr)))
    margin = 0.05 * (cmax - cmin) if cmax > cmin else 0.01
    ax2.set_ylim(max(-1.0, cmin - margin), min(1.0, cmax + margin))

    ax2.set_xlim(0, time_axis[-1] if len(time_axis) > 1 else 1)
    plt.tight_layout()
    save_fig(prefix, f"lag_correlation_{channel_name.lower()}")
    plt.show()
    plt.close('all')


def main():
    parser = argparse.ArgumentParser(
        description="NAE NatAmbio: PCA-based stereo main/ambience decomposition with optional analysis plots.")
    parser.add_argument("wavfile",
                        help="Fichero WAV estéreo a analizar")
    parser.add_argument("--mode", choices=["alpha", "beta"], default=MODE,
                        help="Modo NAE: alpha (main) o beta (ambient). Por defecto: alpha")
    parser.add_argument("--analysis", type=str2bool, default=ANALYSIS,
                        metavar="true|false",
                        help="Modo análisis: genera las gráficas matplotlib (true) o solo procesa (false). Por defecto: true")
    parser.add_argument("--frame-size", type=int, default=1024,
                        help="Tamaño de frame en muestras. Por defecto: 1024")
    parser.add_argument("--covsteps", type=int, default=COVSTEPS,
                        help=f"Número de pasos de covarianza solapados. Por defecto: {COVSTEPS}")
    parser.add_argument("--lag-max", type=int, default=LAG_MAX,
                        help=f"Retraso máximo en muestras aplicado a C1 en el estudio de correlación C1'/C2. Por defecto: {LAG_MAX}")
    parser.add_argument("--lag-step", type=int, default=LAG_STEP,
                        help=f"Paso del barrido de retrasos en muestras. Por defecto: {LAG_STEP}")
    parser.add_argument("--lag-window", type=int, default=0,
                        help="Tamaño en muestras de la ventana de correlación C1'/C2. Por defecto: 0 = frame_size*covsteps (la misma extensión que la ventana de covarianza del PCA)")
    parser.add_argument("--projection", type=str2bool, default=PROJECTION,
                        metavar="true|false",
                        help=f"Genera los WAV de proyección de C1 retardada sobre C2 y su residuo. Por defecto: {str(PROJECTION).lower()}")
    parser.add_argument("--lag-abs", type=str2bool, default=LAG_ABS,
                        metavar="true|false",
                        help="Elegir el retraso por |correlación| (true) o por la correlación con signo (false). true es robusto frente a la ambigüedad de signo de los autovectores del PCA. Por defecto: true")
    parsed = parser.parse_args()

    if parsed.lag_max < 0:
        parser.error("--lag-max debe ser >= 0")
    if parsed.lag_step <= 0:
        parser.error("--lag-step debe ser > 0")
    if parsed.lag_window < 0:
        parser.error("--lag-window debe ser >= 0")

    WAVFILE = parsed.wavfile
    frame_size = parsed.frame_size
    steps = parsed.covsteps
    mode = parsed.mode
    mode_amb = (mode == "beta")
    analysis = parsed.analysis
    projection = parsed.projection
    lags = np.arange(0, parsed.lag_max + 1, parsed.lag_step)
    # Correlation window of the C1'/C2 study: by default the same span as the
    # PCA covariance window (frame_size*steps), advancing one frame at a time.
    lag_window = parsed.lag_window if parsed.lag_window > 0 else frame_size * steps
    label = mode_label(WAVFILE, mode)
    # Prefix for saved figures (and the output WAVs): the WAV path without its
    # extension, so every PNG starts with the WAV name plus a descriptive suffix.
    outprefix = WAVFILE.rsplit(".")[0]

    audio, samplerate = sf.read(WAVFILE)
    if audio.ndim != 2 or audio.shape[1] != 2:
        raise ValueError("Input must be a stereo audio signal.")
    num_samples = audio.shape[0]
    print(f"Successfully loaded stereo WAV file: {WAVFILE}")
    print(f"Sample rate: {samplerate} Hz")
    print(f"Audio samples: {num_samples}")
    print(f"Mode: {mode} | Analysis: {analysis} | Projection: {projection}")

    if analysis:
        # Plot a small portion of the audio
        plt.figure(figsize=FIGSIZE)
        sinit = random.randrange(num_samples-1000)
        plt.plot(audio[sinit:sinit + 1000, 0], label='Left Channel')
        plt.plot(audio[sinit:sinit + 1000, 1], label='Right Channel')
        plt.title(f"Random 1000 samples of each channel\n{label}")
        plt.legend()
        save_fig(outprefix, "samples")
        plt.show()

    end = int(np.floor(num_samples/frame_size))*frame_size
    DataArray = np.zeros((end + frame_size*(steps-1),2))
    pc = np.zeros((end + frame_size*(steps-1),2))
    c1 = np.zeros((end + frame_size*(steps-1),2))
    c2 = np.zeros((end + frame_size*(steps-1),2))
    time = np.arange(0,num_samples,1)/samplerate

    eigenvalue_list = [[],[]]
    angle_list = []
    eigenvectors_1_list = []
    eigenvectors_2_list = []
    correlation_list = []
    l_c1_level = []
    r_c1_level = []
    l_c2_level = []
    r_c2_level = []
    print(f"Processing frame size:{frame_size} steps:{steps}")
    signal = np.zeros([frame_size*steps,2])
    buf = np.zeros([frame_size*(steps - 1),2])
    p_array = np.zeros([frame_size,4])
    p1 = np.zeros((frame_size*steps,2))
    p2 = np.zeros((frame_size*steps,2))
    audiobuf = np.ndarray((frame_size,2))

    print("Start PCA stereo analysis")
    frame_idx = 0
    midside_idx = 0   # counter to give each saved mid/side scatter a unique name

    for start in range(0, end, frame_size):
        end_audio = start + frame_size
        start_pca = start
        end_pca = start_pca + frame_size*steps
        
        if mode_amb:
            icorr = np.corrcoef(audio[max(0,end_audio - ICORRL * frame_size):end_audio,0],audio[max(0,end_audio - ICORRL * frame_size):end_audio,1])[0,1]
            pan = 0.55 + np.abs(icorr) * 0.45
        else:
            pan = 1

        # Correlation calculation
        if analysis:
            alpha = (1.0 - pan)/2
            correlation = np.corrcoef((1.0 - alpha) * audio[start:end_audio,0] + alpha *  audio[start:end_audio,1] ,(1.0 - alpha) * audio[start:end_audio,1] + alpha * audio[start:end_audio,0])
            correlation_list.append(correlation[0,1])
            
        print(f"Start:{start} End:{end_audio} End PCA: {end_pca} len:{end_audio-start} Frame index:{frame_idx} num samples:{num_samples}")
        # Mid component
        signal[:,0] = np.concatenate((buf[:,0],audio[start:end_audio,0] + audio[start:end_audio,1]))
        # Side componet
        signal[:,1] = pan * np.concatenate((buf[:,1],audio[start:end_audio,0] - audio[start:end_audio,1]))
        #frame = signal

        # Transpose before covariance calculation
        centered = np.transpose(signal)
        ER = np.cov(centered)
        eigvalues, eigvectors = np.linalg.eig(ER)

        # Sort eigenvectors by descending eigenvalue
        if eigvalues[0] < eigvalues[1]:
            print("Eigenvalues in reverse order")
            idx = np.argsort(eigvalues)[::-1]
            eigvalues = eigvalues[idx]
            eigvectors = eigvectors[:, idx]
            #eig_tmp = eigvalues[0]
            #eigvalues[0] = eigvalues[1]
            #eigvalues[1] = eig_tmp
            #eigvectors = -1 *eigvectors
        eigenvalue_list[0].append(eigvalues[0] if eigvalues[0] != 0 else 1)
        eigenvalue_list[1].append(eigvalues[1] if eigvalues[1] != 0 else 1)

        PCAArray = np.transpose(eigvectors) @ np.transpose(signal)

        pc[start_pca:end_pca] += np.transpose(PCAArray)
        cov_matrix = np.transpose(ER) @ eigvectors
        p1[:,0] += np.transpose(PCAArray)[:,0] * eigvectors[0,0]
        p1[:,1] += np.transpose(PCAArray)[:,0] * eigvectors[1,0]
        p2[:,0] += np.transpose(PCAArray)[:,1] * eigvectors[0,1]
        p2[:,1] += np.transpose(PCAArray)[:,1] * eigvectors[1,1]

        c1[start_pca:end_pca,0] += (p1[:,0] + p1[:,1])/(steps + 1)
        c1[start_pca:end_pca,1] += (p1[:,0] - p1[:,1])/(steps + 1)
        c2[start_pca:end_pca,0] += (p2[:,0] + p2[:,1])/(steps + 1)
        c2[start_pca:end_pca,1] += (p2[:,0] - p2[:,1])/(steps + 1)

        buf[:,] = signal[frame_size:,]

        p1[:,0] = np.concatenate((p1[frame_size:,0]/steps, np.zeros(frame_size)))
        p1[:,1] = np.concatenate((p1[frame_size:,1]/steps, np.zeros(frame_size)))
        p2[:,0] = np.concatenate((p2[frame_size:,0]/steps, np.zeros(frame_size)))
        p2[:,1] = np.concatenate((p2[frame_size:,1]/steps, np.zeros(frame_size)))

        angle_list.append(eigvectors[0,1]/eigvectors[0,0])
        eigenvectors_1_list.append(eigvectors[0,0])
        eigenvectors_2_list.append(eigvectors[0,1])

        if analysis:
            if frame_idx*frame_size/samplerate > 5:
                # Plot signals
                #print(eigvectors)
                #plt.figure(figsize=(20, 10))
                fig, (ax1, ax2) = plt.subplots(1, 2, figsize=FIGSIZE)
                plt.tight_layout()
                ax1.plot(signal[:,0], label='Mid Channel',color="red",linewidth=0.3)
                ax1.plot(signal[:,1], label='Side Channel',color="blue",linewidth=0.3)
                ax1.set_xlabel("Samples")
                ax1.set_ylabel("Value")
                if mode_amb:
                    ax1.set_title(f"Mid and side channels. NAE mode beta calculation\n{label}")
                else:
                    ax1.set_title(f"Mid and side channels. NAE mode alpha calculation\n{label}")
                ax1.grid(True)
                ax1.legend()
                yaxis_max = max(-1*ax1.get_ylim()[0],ax1.get_ylim()[1])
                ax1.set_ylim(-1*yaxis_max,yaxis_max)
                ax1.set_xlim(0,frame_size)
                v1x = [-1*eigvectors[0,0],eigvectors[0,0]]
                v1y = [eigvectors[0,1],-1*eigvectors[0,1]]
                v2x = [-1*eigvectors[1,0],eigvectors[1,0]]
                v2y = [eigvectors[1,1],-1*eigvectors[1,1]]
                ax2.plot(signal[:,0],signal[:,1],'.',color="black",markersize=1)
                ax2.plot(v1x,v1y,color="red",linestyle='dashed',linewidth=1,label="eigenvector 1")
                ax2.plot(v2x,v2y,color="blue",linestyle='dashed',linewidth=1,label="eigenvector 2")
                ax2.grid(True)
                ax2.set_xlabel("Mid data")
                ax2.set_ylabel("Side data")
                #plt.xlim(-0.125,0.125)
                #plt.ylim(-0.125,0.125)
                ax2.set_xlim(1.2*min(signal[:,0]),1.2*max(signal[:,0]))
                ax2.set_ylim(plt.xlim())
                ax2.set_title(f"Mid vs Side data\n{label}")
                ax2.legend(loc="upper right")
                save_fig(outprefix, f"midside_{midside_idx:03d}")
                midside_idx += 1
                plt.show()
                plt.close('all')
                frame_idx = 0

        frame_idx += 1

    if analysis:
        # Level calculation for 4 components: left_C1, left_C2, right_C1, right_C2
        for start in range(0, end, frame_size):
            end_audio = start + frame_size
            l_c1_level.append(20 * np.log10(max(1.0e-9, np.sum(c1[start:end_audio,0] * c1[start:end_audio,0]))/(end_audio - start)))
            r_c1_level.append(20 * np.log10(max(1.0e-9, np.sum(c1[start:end_audio,1] * c1[start:end_audio,1]))/(end_audio - start)))
            l_c2_level.append(20 * np.log10(max(1.0e-9, np.sum(c2[start:end_audio,0] * c2[start:end_audio,0]))/(end_audio - start)))
            r_c2_level.append(20 * np.log10(max(1.0e-9, np.sum(c2[start:end_audio,1] * c2[start:end_audio,1]))/(end_audio - start)))

    # Saving WAV file for PCA transformation (outprefix computed above)

    # WAV file for component 1
    out_path = f"{outprefix}_c1.wav"
    sf.write(out_path, c1[frame_size*(steps-1):], samplerate)

    # WAV file for component 2
    out_path = f"{outprefix}_c2.wav"
    sf.write(out_path, c2[frame_size*(steps-1):], samplerate)

    if analysis:

        plot_component_levels(l_c1_level,r_c1_level, l_c2_level, r_c2_level, samplerate, frame_size, label, outprefix)
        # Plot for correlation left vs right. Time evolution and histogram
        plot_correlation(correlation_list, samplerate, frame_size, label, outprefix)
        plot_correlation_histogram(correlation_list, frame_size, label, outprefix)
        plot_eigenvectors_rotation(angle_list, samplerate, frame_size, label, outprefix)
        plot_eigenvalues(eigenvalue_list, samplerate, frame_size, label, outprefix)


    # C1/C2 lag-correlation study: delay C1 by every lag in `lags`, correlate
    # the delayed C1' against C2 on each frame, and keep, per frame, the lag
    # that maximises that correlation together with the correlation itself. The
    # projection reuses those per-window lags, so both share the same pass.
    if analysis or projection:
        print(f"Lag-correlation study C1'/C2: lags {lags[0]}..{lags[-1]} "
              f"step {parsed.lag_step} ({len(lags)} lags), window={lag_window} "
              f"({lag_window/frame_size:g} frames), hop={frame_size}")
        proj_stereo = np.zeros_like(c1)
        resid_stereo = np.zeros_like(c2)
        aligned_stereo = np.zeros_like(c1)
        # Left and right are two independent studies, each with its own lag and
        # its own correlation, and each with its own figure.
        for lag_channel, lag_channel_name in ((0, "Left"), (1, "Right")):
            lag_starts, best_lag, best_corr, _ = lag_correlation_study(
                c1, c2, frame_size, lags, lag_channel, window=lag_window,
                hop=frame_size, use_abs=parsed.lag_abs)
            print(f"  {lag_channel_name}: median lag {np.median(best_lag):g} samples, "
                  f"median |correlation| {np.median(np.abs(best_corr)):.4f}")
            if analysis:
                plot_lag_correlation(lag_starts, best_lag, best_corr, samplerate,
                                     lag_window, lags, lag_channel_name,
                                     parsed.lag_abs, label, outprefix)
            if projection:
                # Project the delayed C1 onto C2 using, on each window, the lag
                # that maximised the correlation there.
                p, e, aligned, coeffs = project_delayed_c1_on_c2(
                    c1, c2, lag_starts, best_lag, lag_window, lag_channel)
                proj_stereo[:, lag_channel] = p
                resid_stereo[:, lag_channel] = e
                aligned_stereo[:, lag_channel] = aligned
                print(f"  {lag_channel_name}: projection coefficient a "
                      f"median {np.median(coeffs):+.4f}, "
                      f"range [{coeffs.min():+.4f}, {coeffs.max():+.4f}]")

        if projection:
            # Same trim as the C1/C2 WAVs above, so every output stays aligned.
            head = frame_size*(steps-1)
            # The projection with its delay removed is time-aligned with C1, so
            # it can be subtracted from it: the part of C1 that C2 turned out to
            # be a delayed copy of is taken out of the main component, and C2 is
            # left with its residual.
            c1_minus_stereo = c1 - aligned_stereo
            for signal, suffix in ((proj_stereo, "c1_delayed_projection_on_c2"),
                                   (resid_stereo, "c2_residual"),
                                   (aligned_stereo, "projection_undelayed"),
                                   (c1_minus_stereo, "c1_minus_projection")):
                out_path = f"{outprefix}_{suffix}.wav"
                peak = np.max(np.abs(signal))
                # These are derived signals: C1 minus the projection can exceed
                # full scale even when C1 does not (the coefficient a carries a
                # sign, so the subtraction adds level on the channel where C2 is
                # antiphase). PCM_16 would silently clip that away and corrupt
                # the very thing being measured, so they go out as float.
                if peak > 1.0:
                    print(f"NOTE: {os.path.basename(out_path)} peaks at {peak:.3f} "
                          f"(over full scale; written as float, so nothing is lost, "
                          f"but lower the playback gain)")
                sf.write(out_path, signal[head:], samplerate, subtype="FLOAT")
                print(f"Written {out_path}")


if __name__ == "__main__":
    main()
