"""
fft_convolve.py — FFT convolution of two WAV files.

Reads two WAV files specified on the command line, computes their convolution
via FFT (forward transform of each signal, frequency-domain product, inverse
transform) and writes the result to the specified output WAV.

Usage:
    python fft_convolve.py <wav_1> <wav_2> <output_wav>

Requires only numpy, scipy and soundfile.
"""

import argparse
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve


def read_mono(path):
    """Read a WAV as a 1-D signal (channel 0 if multichannel)."""
    data, samplerate = sf.read(path, dtype="float64")
    if data.ndim > 1:
        data = data[:, 0]
    return data, samplerate


def main():
    parser = argparse.ArgumentParser(
        description="Convolución de dos WAV por FFT (FFT, producto, IFFT).")
    parser.add_argument("wav1", help="Primer WAV de entrada.")
    parser.add_argument("wav2", help="Segundo WAV de entrada.")
    parser.add_argument("output", help="WAV de salida con el resultado.")
    parsed = parser.parse_args()

    x, sr1 = read_mono(parsed.wav1)
    y, sr2 = read_mono(parsed.wav2)
    if sr1 != sr2:
        raise SystemExit(f"Sample-rate mismatch: {sr1} Hz vs {sr2} Hz.")

    # Linear convolution by FFT (length len(x)+len(y)-1).
    result = fftconvolve(x, y)

    # Write 32-bit float: convolution results routinely exceed [-1, 1], so the
    # default 16-bit PCM (which clips and quantizes) must not be used.
    sf.write(parsed.output, result.astype(np.float32), sr1, subtype="FLOAT")
    print(f"Wrote {parsed.output}: {len(result)} samples @ {sr1} Hz")


if __name__ == "__main__":
    main()
