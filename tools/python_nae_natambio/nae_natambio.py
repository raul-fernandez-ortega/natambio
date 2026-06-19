"""
nae_natambio.py — NAE (NatAmbio Ambient Extraction), offline Python version.

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

Because it relies only on numpy, soundfile and matplotlib (all cross-platform),
it runs on **GNU/Linux** and **MS Windows** without modification.

Usage:
    python nae_natambio.py <file.wav> [--ambient true|false]
                                      [--analysis true|false]
                                      [--frame-size N] [--covsteps N]

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
MODE_AMB = False


def str2bool(value):
    """Parse an inline 'true'/'false' argument into a bool."""
    if isinstance(value, bool):
        return value
    if str(value).lower() in ("true", "1", "yes", "y", "t", "on"):
        return True
    if str(value).lower() in ("false", "0", "no", "n", "f", "off"):
        return False
    raise argparse.ArgumentTypeError(f"Expected true/false, got '{value}'")


def mode_label(wavfile, mode_amb):
    """Build a title suffix with the WAV name and the NAE mode."""
    wavname = os.path.basename(wavfile)
    # In graph titles only, "main" mode is shown as "alpha" and "ambient" as "beta".
    mode_str = "NAE beta" if mode_amb else "NAE alpha"
    return f"{wavname} — {mode_str}"


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

def main():
    parser = argparse.ArgumentParser(
        description="NAE NatAmbio: PCA-based stereo main/ambience decomposition with optional analysis plots.")
    parser.add_argument("wavfile",
                        help="Fichero WAV estéreo a analizar")
    parser.add_argument("--ambient", type=str2bool, default=MODE_AMB,
                        metavar="true|false",
                        help="Modo ambiente NAE (true) o modo main (false). Por defecto: false")
    parser.add_argument("--analysis", type=str2bool, default=ANALYSIS,
                        metavar="true|false",
                        help="Modo análisis: genera las gráficas matplotlib (true) o solo procesa (false). Por defecto: true")
    parser.add_argument("--frame-size", type=int, default=1024,
                        help="Tamaño de frame en muestras. Por defecto: 1024")
    parser.add_argument("--covsteps", type=int, default=COVSTEPS,
                        help=f"Número de pasos de covarianza solapados. Por defecto: {COVSTEPS}")
    parsed = parser.parse_args()

    WAVFILE = parsed.wavfile
    frame_size = parsed.frame_size
    steps = parsed.covsteps
    mode_amb = parsed.ambient
    analysis = parsed.analysis
    label = mode_label(WAVFILE, mode_amb)
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
    print(f"Mode: {'ambient' if mode_amb else 'main'} | Analysis: {analysis}")

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


if __name__ == "__main__":
    main()
