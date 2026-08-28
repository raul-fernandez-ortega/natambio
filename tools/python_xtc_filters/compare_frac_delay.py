#! /usr/bin/env python3
#
# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
compare_frac_delay.py — what rounding the ITD to whole samples costs the XTC.

xtc_filters.py rounds the ITD to an integer number of samples because get_xtc()
places its taps at array indices (lib/xtc.c does the same, and so do the two
asymmetric variants). get_xtc_freq() removes that rounding by running the very
same recursion in the frequency domain, where a shift is a linear-phase factor
and a fractional ITD costs nothing. This script measures the difference.

Metric. The recursion's own model of the room is taken as the plant: relative
to the ipsilateral path, the contralateral one is a broadband attenuation
a = 10^(-ild_db/20), the frequency-dependent min-phase ILD filter A(f), and the
*exact* ITD:

    C(f) = a * A(f) * exp(-j*2*pi*f*itd_exact)

Feeding an impulse into the left channel only, the filter pair (D, X) puts

    ipsilateral   S(f) = D + X*C
    contralateral L(f) = X + D*C

at the ears, and the figure of merit is the XTC ratio 20*log10|L/S| — how far
the leak sits below the wanted signal. Lower is better. With no filter at all
(D = 1, X = 0) it degenerates to 20*log10|C|, the acoustic crosstalk, which is
the baseline every row is measured against. The ratio is invariant to a delay
applied to both filters, so the -M model delay of the fractional path does not
have to be compensated before comparing.

Two plants are reported:

  * control (A = 1) — the ILD reduced to its broadband gain. There the
    recursion is exactly the truncated Neumann series of the true 2x2 inverse,
    so with an exact ITD the residual falls to numerical noise and the measured
    curve must follow the closed form 20*log10(a * 2*sin(pi*f*dt)) when the ITD
    is rounded. It exists to check the theory and the implementation, not the
    product.
  * full — the real pipeline, min-phase ILD filter included. This is the number
    that matters.

Usage:
    python3 compare_frac_delay.py [-t ITD_us] [-l ILD_dB] [-a alpha]
                                  [-z azimuth] [-r rate] [-f len]
                                  [-M model_delay] [-p out.png] [--no-sweep]

Everything but the plot needs only numpy; the plot additionally needs
matplotlib, and is skipped with a note if it is not installed.
"""

import math
import sys

import numpy as np

import xtc_filters as X

# Evaluation grid for every transfer function below. Long enough that the
# 4096-tap filters are zero-padded rather than truncated.
NFFT = 1 << 15

# Summary bands, Hz. Stop at 16 kHz: above it the model's 20 kHz shelf
# dominates and the comparison stops meaning anything.
BANDS = [(250, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]


def plant(freqs_hz, ild_db, itd_exact, sample_rate, ild_spec=None):
    """Contralateral-over-ipsilateral transfer C(f) the recursion models."""
    a = math.pow(10.0, -ild_db / 20.0)
    phase = np.exp(-2j * np.pi * freqs_hz * itd_exact / sample_rate)
    return a * phase if ild_spec is None else a * ild_spec * phase


def xtc_ratio_db(direct, cross, plant_c):
    """20*log10|leak / wanted| for the filter pair against plant C."""
    d = np.fft.rfft(direct, n=NFFT)
    x = np.fft.rfft(cross, n=NFFT)
    wanted = d + x * plant_c
    leak = x + d * plant_c
    return 20.0 * np.log10(np.abs(leak) / np.abs(wanted) + 1e-30)


def band_mean_db(freqs_hz, curve_db, lo, hi):
    """Energy mean of a dB curve over [lo, hi)."""
    sel = (freqs_hz >= lo) & (freqs_hz < hi)
    return 10.0 * np.log10(np.mean(np.power(10.0, curve_db[sel] / 10.0)))


def variants(ild_db, itd_exact, filter_len, ild_filter, model_delay):
    """The two filter pairs under test, from the same ILD filter."""
    itd_int = int(round(itd_exact))
    return {
        "integer": X.get_xtc(filter_len, ild_db, itd_int, ild_filter),
        "fractional": X.get_xtc_freq(filter_len, ild_db, itd_exact, ild_filter,
                                     model_delay=model_delay),
    }


def report(title, freqs_hz, base_db, curves, out=sys.stdout):
    """Per-band table: baseline, each variant, and the gain over integer."""
    names = list(curves)
    out.write("\n%s\n" % title)
    out.write("  band            no filter   " +
              "".join("%12s" % n for n in names) + "        gain\n")
    for lo, hi in BANDS:
        row = [band_mean_db(freqs_hz, curves[n], lo, hi) for n in names]
        label = "%5.1f-%-5.1f kHz" % (lo / 1000.0, hi / 1000.0)
        out.write("  %-14s %8.1f dB   " % (label, band_mean_db(freqs_hz, base_db, lo, hi)) +
                  "".join("%9.1f dB" % v for v in row) +
                  "%9.1f dB\n" % (row[0] - row[-1]))


def sweep(ild_db, ild_alpha, azimuth_deg, sample_rate, filter_len, model_delay,
          out=sys.stdout):
    """Improvement vs the rounding error, over a range of ITDs.

    170 us at 48 kHz happens to land at 8.16 samples. The rounding error is
    whatever the parameters make it, uniform in [0, 0.5] samples, so a single
    ITD says little on its own.
    """
    ild = X.build_ild_filter(ild_alpha, azimuth_deg, sample_rate, filter_len)
    freqs_hz = np.fft.rfftfreq(NFFT, 1.0 / sample_rate)
    ild_spec = np.fft.rfft(ild, n=NFFT)
    out.write("\nITD sweep at %d Hz (mean XTC ratio, 4-16 kHz)\n" % sample_rate)
    out.write("  ITD us   samples   round err      integer   fractional      gain\n")
    rows = []
    for itd_us in range(100, 261, 10):
        itd_exact = itd_us * 1e-6 * sample_rate
        err = abs(itd_exact - round(itd_exact))
        c = plant(freqs_hz, ild_db, itd_exact, sample_rate, ild_spec)
        vals = []
        for pair in variants(ild_db, itd_exact, filter_len, ild, model_delay).values():
            curve = xtc_ratio_db(*pair, c)
            vals.append(10.0 * np.log10(np.mean([
                np.power(10.0, band_mean_db(freqs_hz, curve, lo, hi) / 10.0)
                for lo, hi in BANDS if lo >= 4000])))
        out.write("  %6d %9.3f %11.3f %11.1f dB %9.1f dB %7.1f dB\n"
                  % (itd_us, itd_exact, err, vals[0], vals[1], vals[0] - vals[1]))
        rows.append((err, vals[0] - vals[1]))
    return rows


def plot(png, freqs_hz, base_db, curves, ctrl_freqs, ctrl_curves, ctrl_theory,
         sweep_rows, sample_rate):
    """Optional: the tables above are the result, the figure only shows it.

    matplotlib is deliberately not in requirements.txt, so a missing one is a
    note on stderr rather than a traceback over numbers already printed.
    """
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.stderr.write("compare_frac_delay: matplotlib not installed, "
                         "skipping %s (the tables above are unaffected)\n" % png)
        return

    fig, ax = plt.subplots(1, 3, figsize=(16, 4.6))
    sel = (freqs_hz >= 100) & (freqs_hz <= 20000)

    ax[0].semilogx(freqs_hz[sel], base_db[sel], color="0.6", lw=1,
                   label="no filter (acoustic crosstalk)")
    for name, curve in curves.items():
        ax[0].semilogx(freqs_hz[sel], curve[sel], lw=1.2, label="%s ITD" % name)
    ax[0].set_title("Full pipeline, min-phase ILD")

    ax[1].semilogx(ctrl_freqs[sel], ctrl_theory[sel], "k--", lw=1,
                   label=r"theory  $a\cdot2\sin(\pi f\,\Delta\tau)$")
    for name, curve in ctrl_curves.items():
        ax[1].semilogx(ctrl_freqs[sel], curve[sel], lw=1.2, label="%s ITD" % name)
    ax[1].set_title("Control, ILD as broadband gain")

    for a in ax[:2]:
        a.set_xlabel("Hz")
        a.set_ylabel("XTC ratio, dB (lower is better)")
        a.grid(True, which="both", alpha=0.3)
        a.legend(fontsize=8)
        a.set_xlim(100, 20000)
    ax[0].set_ylim(-100, 0)
    ax[1].set_ylim(-90, 0)

    if sweep_rows:
        err, gain = zip(*sweep_rows)
        ax[2].plot(err, gain, "o", ms=4)
        ax[2].set_xlabel("ITD rounding error, samples")
        ax[2].set_ylabel("gain, dB (4-16 kHz)")
        ax[2].set_title("Gain vs rounding error, %d Hz" % sample_rate)
        ax[2].grid(True, alpha=0.3)
    else:
        ax[2].set_visible(False)

    fig.tight_layout()
    fig.savefig(png, dpi=130)
    print("\nwrote %s" % png)


USAGE = ("Usage: python3 compare_frac_delay.py [-t ITD_us] [-l ILD_dB] "
         "[-a alpha] [-z azimuth] [-r rate] [-f len] [-M model_delay] "
         "[-p out.png] [--no-sweep]")


def main():
    cfg = X.default_config()
    side = cfg["xtc"]
    png = "frac_delay_comparison.png"
    do_sweep = True

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--no-sweep":
            do_sweep = False
            i += 1
            continue
        if a in ("-h", "-u"):
            print(USAGE)
            return
        if i + 1 >= len(args):
            sys.stderr.write("%s\n" % USAGE)
            sys.exit(1)
        v = args[i + 1]
        if a == "-t":
            side["itd_us"] = int(v)
        elif a == "-l":
            side["ild_db"] = float(v)
        elif a == "-a":
            side["ild_alpha"] = float(v)
        elif a == "-z":
            side["azimuth_deg"] = int(v)
        elif a == "-r":
            cfg["sample_rate"] = int(v)
        elif a == "-f":
            cfg["filter_len"] = int(v)
        elif a == "-M":
            cfg["model_delay"] = int(v)
        elif a == "-p":
            png = v
        else:
            sys.stderr.write("%s\n" % USAGE)
            sys.exit(1)
        i += 2

    fs = cfg["sample_rate"]
    flen = cfg["filter_len"]
    ild_db = side["ild_db"]
    itd_exact = side["itd_us"] * 1e-6 * fs
    itd_int = int(round(itd_exact))
    err = itd_exact - itd_int
    md = cfg["model_delay"]

    print("ITD %d us at %d Hz = %.4f samples -> rounds to %d "
          "(error %.4f samples = %.2f us)"
          % (side["itd_us"], fs, itd_exact, itd_int, abs(err),
             abs(err) * 1e6 / fs))
    print("ILD %.1f dB, alpha %.1f, azimuth %d deg, %d taps, model delay %d"
          % (ild_db, side["ild_alpha"], side["azimuth_deg"], flen, md))

    freqs_hz = np.fft.rfftfreq(NFFT, 1.0 / fs)

    # --- implementation check: with an integer ITD the frequency-domain
    # recursion must reproduce get_xtc() to numerical precision -------------
    ild = X.build_ild_filter(side["ild_alpha"], side["azimuth_deg"], fs, flen)
    d_t, c_t = X.get_xtc(flen, ild_db, itd_int, ild)
    d_f, c_f = X.get_xtc_freq(flen, ild_db, itd_int, ild)
    worst = max(np.abs(d_t - d_f).max() / np.abs(d_t).max(),
                np.abs(c_t - c_f).max() / np.abs(c_t).max())
    print("get_xtc_freq vs get_xtc at integer ITD: %.1f dB relative"
          % (20 * np.log10(worst)))

    # --- control: ILD reduced to its broadband gain ------------------------
    delta = np.zeros(flen)
    delta[0] = 1.0
    ctrl_plant = plant(freqs_hz, ild_db, itd_exact, fs)
    ctrl_curves = {k: xtc_ratio_db(*v, ctrl_plant) for k, v in
                   variants(ild_db, itd_exact, flen, delta, md).items()}
    ctrl_theory = 20.0 * np.log10(
        math.pow(10.0, -ild_db / 20.0)
        * 2.0 * np.abs(np.sin(np.pi * freqs_hz * err / fs)) + 1e-30)
    report("Control plant (A = 1): isolates the ITD rounding",
           freqs_hz, 20.0 * np.log10(np.abs(ctrl_plant)), ctrl_curves)
    sel = (freqs_hz >= 2000) & (freqs_hz <= 16000)
    print("  integer-ITD curve vs closed form over 2-16 kHz: max deviation "
          "%.2f dB" % np.abs(ctrl_curves["integer"][sel] - ctrl_theory[sel]).max())

    # --- full pipeline -----------------------------------------------------
    ild_spec = np.fft.rfft(ild, n=NFFT)
    full_plant = plant(freqs_hz, ild_db, itd_exact, fs, ild_spec)
    curves = {k: xtc_ratio_db(*v, full_plant) for k, v in
              variants(ild_db, itd_exact, flen, ild, md).items()}
    report("Full pipeline: min-phase ILD filter in the plant and in the filters",
           freqs_hz, 20.0 * np.log10(np.abs(full_plant)), curves)

    rows = sweep(ild_db, side["ild_alpha"], side["azimuth_deg"], fs, flen, md) \
        if do_sweep else []

    if png:
        plot(png, freqs_hz, 20.0 * np.log10(np.abs(full_plant)), curves,
             freqs_hz, ctrl_curves, ctrl_theory, rows, fs)


if __name__ == "__main__":
    main()
