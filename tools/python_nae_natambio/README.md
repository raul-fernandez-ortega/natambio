# python_nae_natambio

*Also available in: [Español](README_es.md)*

**Offline Python** version of the **NAE** (*NatAmbio Ambient Extraction*) algorithm.

It is the same algorithm as the project's other two incarnations, but out of
real time:

| Implementation | Language | Context |
| --- | --- | --- |
| `tools/ladspa_nae_natambio` | C | LADSPA plugin (real time) |
| `src/nae.cpp` | C++ | NAE engine of the `natambio` JACK client (real time) |
| **`tools/python_nae_natambio`** | **Python** | **Offline, over a WAV file** |

This directory is a **self-contained Python project**: it shares no code with the
rest of the repository.

## What it does

It decomposes a stereo signal via PCA (eigenvalues/eigenvectors of the 2×2
covariance matrix over the *mid/side* components, with an overlapping window of
`covsteps` frames) into two components:

- **C1 — main**
- **C2 — ambient**

and writes them as `<input>_c1.wav` and `<input>_c2.wav` next to the input WAV.

It has two purposes:

1. **Run** the NAE algorithm over a WAV reproducibly, without needing a
   real-time audio server (JACK).
2. **Analyse** the process: with `--analysis true` it generates matplotlib plots
   (L/R correlation and its histogram, eigenvector rotation, eigenvalue ratio,
   C1/C2 component levels, and a *mid/side* scatter with the eigenvectors
   overlaid). Each title includes the WAV name and the mode.

## Cross-platform

It relies only on `numpy`, `soundfile` and `matplotlib` —all cross-platform—, so
it runs on both **GNU/Linux** and **MS Windows** without changes.

## Dependencies

- Python 3
- [`numpy`](https://numpy.org/)
- [`soundfile`](https://python-soundfile.readthedocs.io/) (WAV read/write)
- [`matplotlib`](https://matplotlib.org/) (analysis plots)

### Installation

A virtual environment is recommended.

**GNU/Linux**

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install numpy soundfile matplotlib
```

**MS Windows** (PowerShell)

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install numpy soundfile matplotlib
```

> On Windows, `soundfile` bundles the `libsndfile` library; on some GNU/Linux
> distros you may need to install the system package `libsndfile1`.

## Usage

```sh
python nae_natambio.py <file.wav> [--mode alpha|beta]
                                  [--analysis true|false]
                                  [--frame-size N] [--covsteps N]
```

### Arguments

| Argument | Default | Description |
| --- | --- | --- |
| `wavfile` | *(required)* | **Stereo** WAV file to analyse. |
| `--mode` | `alpha` | NAE mode: `alpha` (main) or `beta` (ambient). |
| `--analysis` | `true` | `true` = generate the matplotlib plots; `false` = only process and write the WAVs. |
| `--frame-size` | `1024` | Frame size in samples. |
| `--covsteps` | `5` | Number of overlapped covariance steps. |

The `--analysis` flag accepts `true/false`, `1/0`, `yes/no`, `on/off`.

### Examples

Process in alpha (main) mode with analysis (opens the plots):

```sh
python nae_natambio.py input.wav --mode alpha --analysis true
```

Process in beta (ambient) mode without plots (only generates the output WAVs):

```sh
python nae_natambio.py input.wav --mode beta --analysis false
```

## Output

Next to the input file it generates:

- `<input>_c1.wav` — main component (C1)
- `<input>_c2.wav` — ambient component (C2)

In analysis mode, the matplotlib windows are also shown (each blocks until
closed).
