# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
check_capture.py — quick analysis of a measured sweep capture.

Reads a recorded WAV and prints a status line warning about:
  - CLIPPING (peak >= 0.999, ~0 dBFS),
  - LOW level (peak below `min_level`),
  - low SNR (signal-to-noise ratio below `min_snr`).

The SNR is estimated by comparing the RMS of the whole capture with the RMS
of the leading silence (the first 50 ms, before the sweep arrives).

Intended to be called from measure_pca4drc.sh after each capture. The exit
code indicates whether the capture is valid:
  - 0  -> capture OK (no warnings),
  - 1  -> invalid levels (clipping / low level / low SNR) or capture
          unreadable/empty: the measurement must be repeated.

Usage:
    python check_capture.py <wav> [label] [--min-level -40] [--min-snr 20]

Requires only numpy and soundfile.

Author : Raul Fernandez Ortega
"""

import sys
import math
import argparse
import numpy as np
import soundfile as sf


def main():
    parser = argparse.ArgumentParser(
        description="Analyse a sweep capture and warn about clipping / low "
                    "level / low SNR.")
    parser.add_argument("wav", help="WAV of the capture to analyse.")
    parser.add_argument("label", nargs="?", default="",
                        help="Optional label to identify the capture.")
    parser.add_argument("--min-level", type=float, default=-40.0,
                        metavar="dBFS",
                        help="Low-level threshold in dBFS (default: -40).")
    parser.add_argument("--min-snr", type=float, default=20.0,
                        metavar="dB",
                        help="Low-SNR threshold in dB (default: 20).")
    parsed = parser.parse_args()

    label = parsed.label

    try:
        x, sr = sf.read(parsed.wav, dtype="float64")
    except Exception as e:
        print(f"    [{label}] WARNING: could not read the capture ({e})")
        return 1
    if x.ndim > 1:
        x = x[:, 0]
    if len(x) == 0:
        print(f"    [{label}] WARNING: empty capture")
        return 1

    peak = float(np.abs(x).max())
    peak_db = 20 * math.log10(peak) if peak > 0 else -120.0
    rms = float(np.sqrt(np.mean(x ** 2)))
    # Background noise estimated from the leading silence (before the sweep arrives).
    head = x[: min(len(x), int(0.05 * sr) or 1)]
    noise = float(np.sqrt(np.mean(head ** 2)))
    snr = 20 * math.log10(rms / noise) if (rms > 0 and noise > 0) else None

    warns = []
    if peak >= 0.999:
        warns.append(f"CLIPPING (peak {peak_db:.2f} dBFS)")
    elif peak_db < parsed.min_level:
        warns.append(f"LOW level (peak {peak_db:.1f} dBFS)")
    if snr is not None and snr < parsed.min_snr:
        warns.append(f"low SNR (~{snr:.0f} dB)")

    snr_txt = f", SNR~{snr:.0f} dB" if snr is not None else ""
    status = "OK" if not warns else "*** WARNING: " + "; ".join(warns) + " ***"
    print(f"    [{label}] peak {peak_db:.1f} dBFS{snr_txt} -> {status}")
    return 1 if warns else 0


if __name__ == "__main__":
    sys.exit(main())
