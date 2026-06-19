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
        description="Analiza una captura de sweep y avisa de clipping / nivel "
                    "bajo / SNR baja.")
    parser.add_argument("wav", help="WAV de la captura a analizar.")
    parser.add_argument("label", nargs="?", default="",
                        help="Etiqueta opcional para identificar la captura.")
    parser.add_argument("--min-level", type=float, default=-40.0,
                        metavar="dBFS",
                        help="Umbral de nivel bajo en dBFS (por defecto: -40).")
    parser.add_argument("--min-snr", type=float, default=20.0,
                        metavar="dB",
                        help="Umbral de SNR baja en dB (por defecto: 20).")
    parsed = parser.parse_args()

    label = parsed.label

    try:
        x, sr = sf.read(parsed.wav, dtype="float64")
    except Exception as e:
        print(f"    [{label}] AVISO: no se pudo leer la captura ({e})")
        return 1
    if x.ndim > 1:
        x = x[:, 0]
    if len(x) == 0:
        print(f"    [{label}] AVISO: captura vacía")
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
        warns.append(f"CLIPPING (pico {peak_db:.2f} dBFS)")
    elif peak_db < parsed.min_level:
        warns.append(f"nivel BAJO (pico {peak_db:.1f} dBFS)")
    if snr is not None and snr < parsed.min_snr:
        warns.append(f"SNR baja (~{snr:.0f} dB)")

    snr_txt = f", SNR~{snr:.0f} dB" if snr is not None else ""
    status = "OK" if not warns else "*** AVISO: " + "; ".join(warns) + " ***"
    print(f"    [{label}] pico {peak_db:.1f} dBFS{snr_txt} -> {status}")
    return 1 if warns else 0


if __name__ == "__main__":
    sys.exit(main())
