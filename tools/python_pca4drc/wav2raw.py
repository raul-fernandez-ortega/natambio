# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
wav2raw.py — converts WAV to raw (float 32-bit LE, no header) for DRC.

DRC (Digital Room Correction) reads impulse responses in raw format, headerless:
32-bit float samples in little-endian, mono. This script takes one or more WAV
files and writes `<name>.raw` next to each original.

The conversion is EXACT (dumps the samples as-is, without clipping or
resampling). ecasound is deliberately not used: ecasound clips values outside
[-1, 1], which would corrupt PCA components whose peak can exceed 1.0.

Usage:
    python wav2raw.py <wav> [<wav> ...]
    python wav2raw.py pca4drc/*.wav

Requires only numpy and soundfile.

Author : Raul Fernandez Ortega
"""

import os
import sys
import argparse

import numpy as np
import soundfile as sf


def wav_to_raw(wav_path):
    """Write <wav_path without .wav>.raw with the samples in float32 LE.

    Returns (raw_path, n_samples, samplerate). If the WAV is multichannel,
    channel 0 is used (PCA components are mono).
    """
    data, samplerate = sf.read(wav_path, dtype="float32")
    if data.ndim > 1:
        data = data[:, 0]
    base, _ = os.path.splitext(wav_path)
    raw_path = base + ".raw"
    # '<f4' = float 32-bit little-endian; tofile writes no header.
    np.asarray(data, dtype="<f4").tofile(raw_path)
    return raw_path, len(data), samplerate


def main():
    parser = argparse.ArgumentParser(
        description="Convert WAV to raw float 32-bit LE (the format DRC reads).")
    parser.add_argument("inputs", nargs="+",
                        help="WAV(s) to convert; <name>.raw is created for each.")
    parsed = parser.parse_args()

    for wav_path in parsed.inputs:
        if not os.path.isfile(wav_path):
            print(f"WARNING: '{wav_path}' does not exist; skipping.", file=sys.stderr)
            continue
        raw_path, n, sr = wav_to_raw(wav_path)
        print(f"    {os.path.basename(wav_path)} -> {os.path.basename(raw_path)} "
              f"({n} samples, {sr} Hz, f32 LE)")


if __name__ == "__main__":
    main()
