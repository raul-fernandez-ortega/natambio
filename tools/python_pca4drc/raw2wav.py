# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
raw2wav.py — converts raw (float 32-bit LE, no header) to WAV. Inverse of
wav2raw.py.

DRC works with impulse responses in raw format, headerless: 32-bit float
samples in little-endian, mono. This script takes one or more .raw files and
writes `<name>.wav` (float 32-bit) next to each original. Because the raw has
no header, the sample rate is specified with `--rate`.

The conversion is EXACT (interprets the bytes as float32 LE and writes them
without clipping or resampling), just like wav2raw.py in reverse.

Usage:
    python raw2wav.py <raw> [<raw> ...] [--rate 48000]
    python raw2wav.py pca4drc/*.raw --rate 48000

Requires only numpy and soundfile.

Author : Raul Fernandez Ortega
"""

import os
import sys
import argparse

import numpy as np
import soundfile as sf


def raw_to_wav(raw_path, samplerate):
    """Write <raw_path without .raw>.wav with the float32 samples from the raw.

    Returns (wav_path, n_samples). The raw is interpreted as float 32-bit
    little-endian mono (the same format written by wav2raw.py).
    """
    # '<f4' = float 32-bit little-endian; the raw has no header.
    data = np.fromfile(raw_path, dtype="<f4")
    base, _ = os.path.splitext(raw_path)
    wav_path = base + ".wav"
    # 32-bit float (subtype FLOAT) to preserve the full range without clipping.
    sf.write(wav_path, data, samplerate, subtype="FLOAT")
    return wav_path, len(data)


def main():
    parser = argparse.ArgumentParser(
        description="Convert raw float 32-bit LE (DRC format) to WAV. "
                    "Inverse of wav2raw.py.")
    parser.add_argument("inputs", nargs="+",
                        help="raw(s) to convert; <name>.wav is created for each.")
    parser.add_argument("--rate", "-r", type=int, default=48000,
                        metavar="Hz",
                        help="Sample rate of the generated WAVs "
                             "(the raw does not carry it). Default: 48000")
    parsed = parser.parse_args()

    if parsed.rate <= 0:
        raise SystemExit("--rate must be a positive integer.")

    for raw_path in parsed.inputs:
        if not os.path.isfile(raw_path):
            print(f"WARNING: '{raw_path}' does not exist; skipping.", file=sys.stderr)
            continue
        wav_path, n = raw_to_wav(raw_path, parsed.rate)
        print(f"    {os.path.basename(raw_path)} -> {os.path.basename(wav_path)} "
              f"({n} samples, {parsed.rate} Hz, float)")


if __name__ == "__main__":
    main()
