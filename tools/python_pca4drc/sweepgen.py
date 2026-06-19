"""
sweepgen.py — log-sweep and inverse generation, standalone Python version.

Reimplementation of `~/pyDRC-3.2.3/tools/sweepgen.py`, which delegates the
computation to the C++ function `GlSweep` in `~/pyDRC-3.2.3/baselib_stl.cpp`.
Here the same algorithm is reproduced in pure Python (numpy + soundfile),
WITHOUT depending on the pyDRC library.

Generates a logarithmic frequency sweep (exponential) with Blackman lead-in /
lead-out windows and silence at the start and end, and its inverse filter (the
time-reversed sweep with an exponential decay, normalised) so that its
convolution with the sweep produces an impulse. This is the sweep/inverse pair
consumed by `fft_convolve.py` to extract impulse responses.

Interface identical to the original sweepgen.py (inline arguments):

    python sweepgen.py sweep.xml [-s sweep_filename] [-i inverse_filename]

where `sweep.xml` describes the parameters:

    <generate_sweep>
      <params>
        <sample_rate>44100</sample_rate>
        <amplitude>0.5</amplitude>
        <Hzstart>20</Hzstart>
        <Hzend>20000</Hzend>
        <length>6</length>        <!-- sweep duration, s -->
        <silence>3</silence>      <!-- silence at start and end, s -->
        <leadin>0.05</leadin>     <!-- fraction of sweep with lead-in window -->
        <leadout>0.005</leadout>  <!-- fraction of sweep with lead-out window -->
      </params>
      <sweep_filename>sweep_02.wav</sweep_filename>
      <inverse_filename>inverse_02.wav</inverse_filename>
    </generate_sweep>

`-s` / `-i` allow overriding the filenames from the XML.

WAVs are written as 32-bit floating point (same as pyDRC). Requires only numpy
and soundfile, so it runs on GNU/Linux and MS Windows without modification.

Author : Raul Fernandez Ortega
"""

import sys
import xml.etree.ElementTree as ET

import numpy as np
import soundfile as sf

M_2PI = 2.0 * np.pi


def blackman_coef(c1, c2, idx):
    """Blackman window coefficient (same formula as GlSweep).

    GlSweep builds the lead-in/lead-out windows sample by sample with
    0.42 - 0.5*cos(c1*idx) + 0.08*cos(c2*idx), where c1 = pi/(L-1) and
    c2 = 2*pi/(L-1) for a window of length L. `idx` may be an array.
    """
    return 0.42 - 0.5 * np.cos(c1 * idx) + 0.08 * np.cos(c2 * idx)


def gl_sweep(rate, amplitude, hz_start, hz_end, duration, silence,
             lead_in, lead_out):
    """Generate the log-sweep and its inverse (port of pyDRC's GlSweep).

    Returns (sweep_signal, inverse_signal) as float64 1-D arrays. The sweep
    includes `silence` seconds of zeros at the start and end; the inverse does
    not (its length is exactly the sweep duration in samples).
    """
    sweep_len = int(rate * duration)
    silence_len = int(rate * silence)
    w1 = hz_start * M_2PI
    w2 = hz_end * M_2PI
    ratio = np.log(w2 / w1)
    s1 = (w1 * duration) / ratio
    s2 = ratio / sweep_len
    decay_time = sweep_len * np.log(2.0) / ratio

    lead_in_len = int(lead_in * sweep_len)
    wc1_in = np.pi / (lead_in_len - 1)
    wc2_in = M_2PI / (lead_in_len - 1)
    lead_out_len = int(lead_out * sweep_len)
    wc1_out = np.pi / (lead_out_len - 1)
    wc2_out = M_2PI / (lead_out_len - 1)

    print(f"\nSweep length: {sweep_len} samples")
    print(f"Silence length: {silence_len} samples")
    print(f"Total sweep length: {2 * silence_len + sweep_len} samples")
    print(f"Total inverse length: {sweep_len} samples\n")

    # --- Base sweep ----------------------------------------------------------
    # Instantaneous sample of the exponential sweep at index I in [0, sweep_len).
    idx = np.arange(sweep_len)
    sample = np.sin(s1 * (np.exp(idx * s2) - 1.0))

    # Lead-in / lead-out Blackman envelope (1.0 in the middle "full sweep" part).
    window = np.ones(sweep_len)
    if lead_in_len > 0:
        i_in = np.arange(lead_in_len)
        window[:lead_in_len] = blackman_coef(wc1_in, wc2_in, i_in)
    if lead_out_len > 0:
        # I = sweep_len-lead_out_len+k, J = lead_out_len-k for k in [0,lead_out_len)
        j_out = lead_out_len - np.arange(lead_out_len)
        window[sweep_len - lead_out_len:] = blackman_coef(wc1_out, wc2_out, j_out)

    sweep_core = sample * window * amplitude
    sweep_signal = np.concatenate([
        np.zeros(silence_len),
        sweep_core,
        np.zeros(silence_len),
    ])

    # --- Inverse filter ------------------------------------------------------
    # Time-reversed sweep (J = sweep_len - I) with an exponential decay
    # 0.5^(I/decay_time) and the matching Blackman tails.
    inv_idx = np.arange(sweep_len)
    j = sweep_len - inv_idx
    decay = np.power(0.5, inv_idx / decay_time)
    inv_sample = np.sin(s1 * (np.exp(j * s2) - 1.0))

    inv_window = np.ones(sweep_len)
    if lead_out_len > 0:
        # First lead_out_len samples: Blackman indexed by I (0..lead_out_len-1).
        i1 = np.arange(lead_out_len)
        inv_window[:lead_out_len] = blackman_coef(wc1_out, wc2_out, i1)
    if lead_in_len > 0:
        # Last lead_in_len samples: Blackman indexed by J = sweep_len-I (lead_in_len..1).
        tail = np.arange(sweep_len - lead_in_len, sweep_len)
        j_tail = sweep_len - tail
        inv_window[sweep_len - lead_in_len:] = blackman_coef(wc1_in, wc2_in, j_tail)

    rmsv = inv_sample * inv_window * decay
    rms = float(np.sum(rmsv ** 2))
    print(f"Normalization factor: {rms:f}")
    norm = 0.5 / rms
    inverse_signal = norm * inv_sample * inv_window * decay

    return sweep_signal, inverse_signal


def parse_params(xml_path):
    """Read the <generate_sweep> XML and return (params, sweep_file, inv_file)."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "generate_sweep":
        raise SystemExit("generate_sweep tag not found")

    params = None
    for child in root:
        if child.tag == "params":
            params = dict(
                sample_rate=int(child.find("sample_rate").text),
                amplitude=float(child.find("amplitude").text),
                hz_start=float(child.find("Hzstart").text),
                hz_end=float(child.find("Hzend").text),
                duration=float(child.find("length").text),
                silence=float(child.find("silence").text),
                lead_in=float(child.find("leadin").text),
                lead_out=float(child.find("leadout").text),
            )
    if params is None:
        raise SystemExit("params tag not found")

    sweep_file = root.find("sweep_filename").text
    inverse_file = root.find("inverse_filename").text
    return params, sweep_file, inverse_file


def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("python sweepgen.py sweep.xml [-s sweep_filename] [-i inverse_filename]")
        sys.exit()

    params, sweep_file, inverse_file = parse_params(sys.argv[1])

    # Optional -s / -i overrides of the output filenames (as in the original).
    for i in range(2, len(sys.argv)):
        if sys.argv[i] == "-s":
            sweep_file = sys.argv[i + 1]
        elif sys.argv[i] == "-i":
            inverse_file = sys.argv[i + 1]

    print(f"Saving sweep signal at {sweep_file} and inverse signal at {inverse_file}")
    print("Sweep parameters:")
    print(f"\tSample rate:{params['sample_rate']} Hz.")
    print(f"\tAmplitude:{params['amplitude']:.2f}.")
    print(f"\tStarts at {params['hz_start']:.2f} Hz.")
    print(f"\tEnds at {params['hz_end']:.2f} Hz.")
    print(f"\tLength {params['duration']:.2f} s.")
    print(f"\tSilence {params['silence']:.2f} s at start and end.")
    print(f"\tLead in time fraction {params['lead_in']:.3f}")
    print(f"\tLead out time fraction {params['lead_out']:.3f}")

    sweep_signal, inverse_signal = gl_sweep(
        params["sample_rate"], params["amplitude"],
        params["hz_start"], params["hz_end"],
        params["duration"], params["silence"],
        params["lead_in"], params["lead_out"],
    )

    # 32-bit float (PcmFloat32Bit in pyDRC) to preserve the full range.
    sf.write(sweep_file, sweep_signal.astype(np.float32),
             params["sample_rate"], subtype="FLOAT")
    sf.write(inverse_file, inverse_signal.astype(np.float32),
             params["sample_rate"], subtype="FLOAT")

    print("Done.")


if __name__ == "__main__":
    main()
