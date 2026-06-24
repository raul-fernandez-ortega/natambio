# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
pca4drc.py — PCA of impulse responses for DRC, standalone Python version.

Reimplementation of `~/pyDRC-3.2.3/pca.py` that does NOT depend on the pyDRC
library and generates NO graphical analysis. It only performs the processing
and saves the resulting PCA components in WAV format.

Algorithm:

  1. Reads all impulse responses (.wav) from an input directory with soundfile.
  2. Locates the peak (absolute maximum) of each impulse.
  3. Rewrites each impulse as a signal of length `output_len` centred on its
     peak, and applies a Blackman window (same as pca.py).
  4. Subtracts the mean of each impulse and computes the PCA transform:
     covariance matrix between impulses, eigenvalues/eigenvectors (sorted
     descending) and projection of the impulses onto the eigenvectors. Component
     0 is always the principal one.
  5. Saves each PCA component (length `output_len`) as a WAV, normalised by the
     peak of the principal component and with corrected polarity, numbered by
     their algorithm order (PCA_0.wav, PCA_1.wav, ...), in a `pca4drc/`
     subdirectory inside the input directory (created if it does not exist).

Usage:
    python pca4drc.py <impulse_directory> <output_len>

Requires only numpy and soundfile (cross-platform): runs on GNU/Linux and
MS Windows without modification. This directory (tools/python_pca4drc) is a
standalone Python project: it shares no code with the rest of the repository.

Author : Raul Fernandez Ortega
"""

import os
import sys
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


class PCA:
    """PCA decomposition of a set of impulse responses (pyDRC-free)."""

    def __init__(self, impulse_dir, output_len):
        self.impulse_dir = impulse_dir
        self.output_len = output_len

        # Collect .wav files (sorted for reproducible component ordering).
        self.DataFileList = []
        for fileName in sorted(os.listdir(impulse_dir)):
            if fileName.lower().endswith(".wav"):
                print(f"Found impulse file {fileName}")
                self.DataFileList.append(os.path.join(impulse_dir, fileName))
        self.Size = len(self.DataFileList)
        self.SampleRate = None
        self.DataArray = np.zeros((self.Size, self.output_len), np.double)
        self.PCAArray = None

        # Nothing to decompose with fewer than 2 impulses: skip the (costly)
        # reading/windowing; main() reports the skip and exits cleanly.
        if self.Size < 2:
            return

        window = np.blackman(self.output_len)

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
            # Center on the peak to output_len, then apply the Blackman window.
            centered = center_on_peak(signal, peak_pos, self.output_len)
            self.DataArray[i] = centered * window
        print(f"Data File max length: {max_length}")

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


def main():
    parser = argparse.ArgumentParser(
        description="PCA of impulse responses for DRC (no pyDRC, no plots).")
    parser.add_argument("impulse_dir",
                        help="Directory with the measured impulse responses in WAV format.")
    parser.add_argument("output_len", type=int,
                        help="Length (in samples) of the generated WAVs; each impulse is "
                             "centred on its peak to this length.")
    parser.add_argument("--normalize", type=str2bool, default=True,
                        metavar="true|false",
                        help="Normalize the components by the peak of the principal one (true) "
                             "or save the raw PCA values (false). Default: true")
    parsed = parser.parse_args()

    if not os.path.isdir(parsed.impulse_dir):
        raise SystemExit(f"'{parsed.impulse_dir}' is not a directory.")
    if parsed.output_len <= 0:
        raise SystemExit("output_len must be a positive integer.")

    pca = PCA(parsed.impulse_dir, parsed.output_len)
    # PCA needs at least 2 impulses (one observation has no covariance). With
    # fewer, skip with a warning but exit cleanly so a batch pipeline continues.
    if pca.Size < 2:
        print(f"WARNING: only {pca.Size} impulse(s) in '{parsed.impulse_dir}'; "
              f"PCA needs at least 2. Skipping PCA.", file=sys.stderr)
        return
    pca.process()
    # Output goes into a 'pca4drc' subdirectory of the input directory.
    pca_dir = os.path.join(parsed.impulse_dir, "pca4drc")
    pca.save_PCA_components(pca_dir, normalize=parsed.normalize)
    print(f"Done. {pca.Size} PCA components written to {pca_dir}")


if __name__ == "__main__":
    main()
