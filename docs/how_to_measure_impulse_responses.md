# How to measure impulse responses for DRC with NatAmbio

*Also available in: [Español](como_medir_respuestas_impulsivas.md)*

To apply equalization by convolution of FIR filters — what is known as Digital Room Correction (DRC) — you need impulse measurements of the room's response for each loudspeaker to be equalized. This is independent of the convolver finally used, be it NatAmbio or any other, and of the process used to obtain the DRC FIR filter (in my case I always use [DRC-FIR](http://drc-fir.sourceforge.net)).

In addition, together with NatAmbio, a [proposal for room impulse measurement based on taking numerous captures at different points of a listening area and, by applying PCA](pca4drc/pca4drc_en.md), characterizing the measurements into a single impulse that will be the target to invert is presented.

This guide explains, in a basic way, how to measure the impulse responses of a NatAmbio system in the listening room and, from them, obtain by PCA a reference impulse with which to generate the room-correction (DRC) FIR filters. This guide applies both to the traditional single-measurement case and to the more advanced multipoint measurement and characterization via PCA4DRC. It includes measurements over single-dipole and two-dipole systems, as well as the addition of a subwoofer.

In addition, for all of the above options, an automation is provided with the script [`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh), which chains the tools of the [`tools/python_pca4drc/`](../tools/python_pca4drc/README.md) toolkit and makes the orderly measurement process easier.

As already noted, the foundation of the method (multipoint measurement + PCA) is developed in the article [Application of PCA to impulsive acoustic measurements of loudspeakers](pca4drc/pca4drc_en.md). On the other hand, in the proposed automation, the generation of the DRC filters themselves is performed by [DRC-FIR](https://drc-fir.sourceforge.net/) by Denis Sbragion (an external program).

## Multipoint measurement concept

Instead of measuring the impulse response at a single listening point — whose representativeness could be debatable — **several measurements** are taken across the listening region (by default the automation defines **16 positions**) and a **Principal Component Analysis (PCA)** is applied to the set. The **principal component** (`PCA_0`) condenses the acoustic information common to the whole region and attenuates the less-correlated phenomena (e.g. reflections whose contribution changes significantly from one measurement position to another). That principal component is the one used as the reference impulse to generate the DRC filter.

## Measurement microphones

To measure the aforementioned impulses correctly an omnidirectional microphone is required. There are two types of microphone that can be used:

- The classic ones, which need to be connected to a microphone preamp that powers them at 48 V and correctly sets the gain to the usual levels for this type of microphone. For example, mine is a very basic [Behringer ECM 8000](https://www.behringer.com/en/products/0506-AAA). In this field the range is wide and prices and qualities vary greatly.
- Microphones that already include the preamp functionality built in and connect directly via USB to the measurement computer. The [MiniDSP Umik-2](https://www.minidsp.com/products/acoustic-measurement/umik-2) model is very popular.

If you want to measure with high precision it is essential that the individual calibration curve is supplied with the microphone. With this table or graph, the obtained measurement can be corrected to get values with less error.

Classic omnidirectional microphones require a preamp that is usually part of the professional audio interfaces [such as those recommended for NatAmbio](hw_setup_en.md). Therefore, the audio interface itself already provides the ability to measure together with the microphone connected to it. These interfaces have a HW or SW switch for phantom power and physical input gain controls; in addition, their connection is always XLR.

## What is a log-sweep

Log-sweeps are measurement signals widely used in this type of impulse-acquisition processes. They allow impulses characteristic of acoustic environments to be obtained with excellent SNR.
A log-sweep is a sinusoidal sweep whose frequency increases progressively in a logarithmic fashion between two defined limits. This time distribution makes the energy spread out in a way that is especially well suited to acoustic measurements with a large dynamic range. It is admittedly not a very pleasant signal to listen to, but it is softer than white noise.

What makes them very interesting is that log-sweeps have a "sister signal" that traverses the frequencies in reverse and with inversely proportional energy. And when both are convolved together the result is a perfect linear-phase impulse bounded to the defined measurement range.

It is the convolution of this inverse with the actual measurements that yields the impulses sought. Therefore, measuring with the log-sweep is the first essential step, but, to finally obtain the impulse, you must also convolve the measurement with the inverse filter.

## Before measuring

> **Important warning**: throughout the measurement process you must keep the playback levels of the tonal sweep signals under control to avoid accidents. To this end it is very convenient to do a prior calibration: if the system has global or per-dipole volume controls, start at a low level and raise it up to the point at which a correct level is reached. This is achieved by combining the hardware playback and recording levels with the software levels of the measurement program.

Before measuring you must prepare the whole physical and software environment:

- Locate, on GNU/Linux, the audio interface to use and start jackd with it.
- Prepare the microphone, placed on its mic stand and connected via XLR to the microphone-preamp input of the NatAmbio audio interface.
- Identify in jackd the names of the microphone input and the outputs to each loudspeaker.

The following is an example for the Focusrite Scarlett 6i6 audio interface:

![Focusrite Scarlett 6i6](figs/focusrite_scarlett_v01.svg)

Usually, the microphone inputs of external audio interfaces correspond to the first ones in jackd's "capture" element list.

### Locating the audio interface

If the interface is USB, which is the most common, it is easy to locate it with:

```
$cat /proc/asound/cards
 0 [PCH            ]: HDA-Intel - HDA Intel PCH
                      HDA Intel PCH at 0x6001120000 irq 144
 2 [USB            ]: USB-Audio - Scarlett 6i6 USB
                      Focusrite Scarlett 6i6 USB at usb-0000:00:14.0-5.4, high speed
```

```
$aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: PCH [HDA Intel PCH], device 0: ALC3266 Analog [ALC3266 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 3: HDMI 0 [HDMI 0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 7: HDMI 1 [HDMI 1]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: PCH [HDA Intel PCH], device 8: HDMI 2 [HDMI 2]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: USB [Scarlett 6i6 USB], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0

```

Once the interface is identified, jackd can be started as follows:

``` 
/usr/bin/jackd -R -P70 -dalsa -dhw:USB -r<SAMPLERATE> -p<BUFFERSIZE> -n3
```

Another possibility is to use the graphical application **[qjackctl](https://qjackctl.sourceforge.io/)**.

The interface's I/O ports usually appear in jackd with the following names:

![Jackd Focusrite Scarlett 6i6](./figs/focusrite_scarlett_natambio.png)

In the Focusrite Scarlett 6i6 example, the microphone inputs correspond to ``system:capture_1`` and ``system:capture_2``.

### NatAmbio in bypass and subwoofer mode

One way to standardize the measurement process is to always have a NatAmbio session running. This way the log-sweep is not sent directly to an output of the audio interface, but to a NatAmbio input. For the case of subwoofer management from NatAmbio, this step is essential, since the measurement must be done with the low-pass and high-pass filters active and sent to the monitors and the subwoofer respectively.

Inside the pca4drc measurement-tools folder there is a set of XML configuration files to take measurements with NatAmbio in bypass / subwoofer-management mode.

There are **four** configuration files, one for each system to measure, all in [`tools/python_pca4drc/`](../tools/python_pca4drc/). The name follows the pattern `{half,full}_natambio_measurements_{normal,subwoofer}.xml`: `half` = one dipole (front only), `full` = two dipoles (front + rear); `normal` = without subwoofer, `subwoofer` = with subwoofer. The script chooses the XML automatically according to the variables `FULL_NATAMBIO` and `SUBWOOFER`:

| System to measure | XML file (in `tools/python_pca4drc/`) | `measure_pca4drc.sh` variables | natambio outputs to connect to the card |
|---|---|---|---|
| One dipole | `half_natambio_measurements_normal.xml` | `FULL_NATAMBIO=false` `SUBWOOFER=false` | `front_output_left`, `front_output_right` |
| One dipole with subwoofer | `half_natambio_measurements_subwoofer.xml` | `FULL_NATAMBIO=false` `SUBWOOFER=true` | `high_pass_front_output_{left,right}` (front speakers) + `low_pass_front_output_{left,right}` (subwoofer) |
| Two dipoles | `full_natambio_measurements_normal.xml` | `FULL_NATAMBIO=true` `SUBWOOFER=false` | `front_output_{left,right}` + `rear_output_{left,right}` |
| Two dipoles with subwoofer | `full_natambio_measurements_subwoofer.xml` | `FULL_NATAMBIO=true` `SUBWOOFER=true` | `high_pass_front_output_{left,right}` + `low_pass_front_output_{left,right}` + `rear_output_{left,right}` |

> In the **subwoofer** variants, the *front* dipole is split into a **high-pass** output (to the front speakers) and a **low-pass** output (to the subwoofer); the *rear* dipole (in `full`) is not split.

In any of the four files, an adjustment must be made to send NatAmbio's output signals to the correct outputs of the audio interface: assign the `<destname>` of each output (`<jack_output>`) to the physical `system:playback_*` port of the card to which that loudspeaker is connected. For example, for `half_natambio_measurements_normal.xml` (one dipole):

```
    <jack_output> 
      <port>
	<name>front_output_left</name>
	<destname>system:playback_1</destname>
      </port>
      <port>
	<name>front_output_right</name>
	<destname>system:playback_2</destname>
      </port>   
    </jack_output>

```
It is a matter of assigning the `<destname>` of the `<front_output_left>` and `<front_output_right>` channels so that the tonal sweeps are heard through the corresponding loudspeaker.

Running natambio is straightforward, with jackd already active:

```
natambio [-quiet] <config.xml>
```
The -quiet parameter disables the output messages.

### Generating the measurement sweep

The excitation signal is a **logarithmic sinusoidal sweep** (log-sweep). To later deconvolve the recording and obtain the impulse response, **two files** are needed: the sweep itself and its **inverse filter**. Both are generated at once by the [`sweepgen.py`](../tools/python_pca4drc/sweepgen.py) tool (it is Phase 0 of the automated flow, and `measure_pca4drc.sh` runs it on its own unless it is skipped with `DO_SWEEP=0`).

`sweepgen.py` takes its parameters from a `<generate_sweep>` XML. Create a file, for example `sweep.xml`, with the content:

```xml
<generate_sweep>
  <params>
    <sample_rate>48000</sample_rate>   <!-- MUST match the JACK capture rate -->
    <amplitude>0.5</amplitude>         <!-- sweep peak amplitude -->
    <Hzstart>20</Hzstart>              <!-- start frequency -->
    <Hzend>20000</Hzend>               <!-- end frequency -->
    <length>6</length>                 <!-- sweep duration, s -->
    <silence>1</silence>               <!-- silence at start and end, s -->
    <leadin>0.05</leadin>              <!-- fraction of the sweep with a fade-in window -->
    <leadout>0.005</leadout>           <!-- fraction of the sweep with a fade-out window -->
  </params>
  <sweep_filename>sweep_48k.wav</sweep_filename>
  <inverse_filename>inverse_48k.wav</inverse_filename>
</generate_sweep>
```

And it is run with:

```sh
python sweepgen.py sweep.xml
```

This generates `sweep_48k.wav` (the excitation sweep, with its silences) and `inverse_48k.wav` (the inverse filter, exactly the length of the sweep). The XML file names can be overridden from the command line with `-s` (sweep) and `-i` (inverse):

```sh
python sweepgen.py sweep.xml -s sweep_48k.wav -i inverse_48k.wav
```

> **Important**: `sample_rate` (48000 Hz in the example) must match the sample rate at which JACK will record. The sweep generated this way (`sweep_48k.wav`) is exactly the one played in the `ecasound` example of the next section, and the inverse (`inverse_48k.wav`) the one later used to deconvolve.

### Taking the measurements manually

To take the measurements manually on GNU/Linux a very useful and flexible command is `ecasound`.

The idea is to launch two audio chains at the same time within the same `ecasound` run:

- A playback chain that sends the sweep (`sweep_48k.wav`) to the natambio input of the way to be measured.
- A recording chain that captures the microphone (`system:capture_1`) and writes it to a WAV.

Assuming JACK and natambio are already running (natambio with the measurement configuration, exposing its `natambio:*_input_*` ports), a measurement of the *front left* way would be:

```sh
ecasound -t:10 \
    -a:1 -i sweep_48k.wav -a:1 -o:jack_auto,natambio:front_input_left -a:1 -eadb:0 \
    -a:2 -i:jack_auto,system:capture_1 -a:2 -f:f32_le,1,48000 \
    -o:left_sweep_1.wav -a:2 -eadb:10 -ev
```

Breakdown of the parameters (identical to those of the script):

| Parameter | Meaning |
|---|---|
| `-t:10` | capture duration in seconds (`REC_SECONDS`) |
| `-a:1` | chain 1 = sweep **playback** |
| `-i sweep_48k.wav` | input of chain 1: the sweep file (`SWEEP`) |
| `-o:jack_auto,natambio:front_input_left` | output of chain 1: auto-connects to the natambio input of that way (`OUT_PORTS`) |
| `-eadb:0` (chain 1) | **output** gain in dB (`GAIN_OUT`) |
| `-a:2` | chain 2 = microphone **recording** |
| `-i:jack_auto,system:capture_1` | input of chain 2: auto-connects to the microphone input (`IN_MEAS`) |
| `-f:f32_le,1,48000` | format of the recorded WAV: float 32-bit, mono, 48 kHz |
| `-o:left_sweep_1.wav` | output of chain 2: the WAV with the recorded response |
| `-eadb:10` (chain 2) | **input** gain in dB (`GAIN_IN`) |
| `-ev` | analyses peaks at the end (useful to see whether there was clipping) |

The `jack_auto` is what performs the **auto-connection**: `ecasound` creates its JACK port and automatically connects it to the indicated port (the natambio input for playback, the microphone capture for recording).

The `-ev` parameter is included so that ecasound generates a table of measured levels, so you can analyse whether the signal's peak and average are too low or too close to saturation.

To measure **another way** just change the output port (e.g. `natambio:front_input_right`) and the WAV name. And to measure **without going through natambio** (directly to a loudspeaker on the card), replace the output with the corresponding physical port, e.g. `-o:jack_auto,system:playback_1`.

This capture is still the *recorded sweep*: to obtain the **impulse response** it must be deconvolved with the inverse sweep, which is exactly what the `fft_convolve.py` script does (Phase 2 of the automated flow):

```sh
python fft_convolve.py left_sweep_1.wav inverse_48k.wav front_left_impulse_1.wav
```

### Is the measurement correct?

There are a few basic checks to make sure the measurement taken is correct and guarantees obtaining a correct impulse:

1. The log-sweep should be heard through the loudspeakers at a reasonably high level, perhaps a bit above the usual level when listening to music. But without needing to become very annoying.
2. In the values of ecasound's `-ev,m` analysis it is advisable to keep a 10 dB headroom, so that **[Example output pending]**

The pca4drc helper script ``check_capture.py`` makes it possible to check the correct levels from the analysis of the measurement WAV itself, before converting it to an impulse. This analysis includes checking that there was no level clipping and that the SNR is sufficient (typically 20 dB).

### Table of basic scripts and commands for manual measurement

Summary of the tools involved in a manual measurement, in the order in which they are used. The scripts `sweepgen.py` and `fft_convolve.py` are part of the [`tools/python_pca4drc/`](../tools/python_pca4drc/README.md) toolkit; `ecasound` is an external system tool.

| Step | Tool | Belongs to | What it does | Example |
|---|---|---|---|---|
| 1. Generate the sweep | `sweepgen.py` | `tools/python_pca4drc/` | Generates the excitation log-sweep and its inverse filter from the `<generate_sweep>` XML | `python sweepgen.py sweep.xml -s sweep_48k.wav -i inverse_48k.wav` |
| 2. Measure (play and record) | `ecasound` | external (system) | Plays the sweep through one way and records the microphone response into a WAV | `ecasound -t:10 -a:1 -i sweep_48k.wav -a:1 -o:jack_auto,natambio:front_input_left -a:1 -eadb:0 -a:2 -i:jack_auto,system:capture_1 -a:2 -f:f32_le,1,48000 -o:left_sweep_1.wav -a:2 -eadb:10 -ev` |
| 2b. Validate the capture (optional) | `check_capture.py` | `tools/python_pca4drc/` | Analyses the recorded WAV and warns about clipping, low level or low SNR before continuing | `python check_capture.py left_sweep_1.wav "front left"` |
| 3. Obtain the impulse | `fft_convolve.py` | `tools/python_pca4drc/` | Deconvolves the recorded sweep with the inverse to obtain the impulse response | `python fft_convolve.py left_sweep_1.wav inverse_48k.wav front_left_impulse_1.wav` |

#### Using `check_capture.py`

After each recording it is advisable to check that the levels are correct **before** processing it to obtain the impulse, or before moving the microphone if you are in a multipoint measurement process. That is what `check_capture.py` does: it reads the captured WAV and warns about three common problems.

```sh
python check_capture.py <wav> [label] [--min-level -40] [--min-snr 20]
```

- `<wav>`: the capture file to analyse (e.g. `left_sweep_1.wav`).
- `[label]` (optional): text to identify the capture in the message (e.g. `"front left"`).
- `--min-level` (default `-40` dBFS): threshold below which it warns about **low level**.
- `--min-snr` (default `20` dB): threshold below which it warns about **low SNR**.

What it checks:

- **Clipping**: if the peak reaches ~0 dBFS (≥ 0.999). You must **lower** the gain.
- **Low level**: if the peak is below `--min-level`. You must **raise** the gain (of the microphone preamp and/or `GAIN_IN`).
- **Low SNR**: signal-to-noise ratio below `--min-snr`. It estimates it by comparing the RMS of the whole capture with that of the **leading silence** (the first 50 ms, before the sweep arrives); that is why it is important to keep the leading `<silence>` when generating the sweep. A low SNR usually indicates too much background noise or an insufficient sweep level.

The result is printed on one line, for example:

```
    [front left] peak -6.2 dBFS, SNR~48 dB -> OK
    [front left] peak -0.0 dBFS, SNR~45 dB -> *** WARNING: CLIPPING (peak -0.01 dBFS) ***
```

In addition it returns an **exit code** useful for automation: `0` if the capture is valid and `1` if there is any warning (or the WAV cannot be read / is empty), so that `measure_pca4drc.sh` uses it to not advance and to ask for the measurement to be repeated. For example, in your own script:

```sh
if python check_capture.py left_sweep_1.wav "front left"; then
    echo "Valid capture; continuing with the deconvolution."
else
    echo "Incorrect levels: readjust the gain and repeat the measurement."
fi
```

### Applying DRC-FIR

Once the reference impulse response of each channel has been obtained, the last step is to **invert it** to generate the FIR correction filters. The recommended application for the NatAmbio case is [DRC-FIR](https://drc-fir.sourceforge.net/).

#### Files DRC uses

DRC is controlled with a configuration file, [`config.drc`](../tools/python_pca4drc/config.drc), which offers a huge and very flexible set of parameters. DRC's documentation is exhaustive, so in these sections devoted to this program a summary of its application to NatAmbio is given.

When using DRC with the goal of obtaining equalization filters applicable to NatAmbio, the basic parameters of its execution are:

| File | Key in `config.drc` | What it is | Included? |
|---|---|---|---|
| **Input** impulse | `BCInFile` (`pca4drc/PCA_0.raw`, or the impulse measured with a single capture) | The reference impulse response to invert | Generated by the measurement (Phases 2–3) |
| **Target** curve | `PSPointsFile` (`../target/48.0 kHz/subultra-48.0.txt`) | The desired frequency response after correction | Yes, in [`tools/python_pca4drc/target/<rate>/`](../tools/python_pca4drc/target/) |
| **Microphone** correction | `MCPointsFile` (`wm-61a.txt`) | Microphone calibration curve, to discount its response | **No**; it also comes **disabled** by default |
| **Output** FIR filters | `PSOutFile` (`rps.raw`), `MSOutFile` (`rms.raw`) | The resulting correction filters (minimum phase / linear phase) | Generated by DRC |

Important notes:

- **Target**: the toolkit includes a set of target curves per sample rate (`target/44.1 kHz/`, `target/48.0 kHz/`, etc.). You must choose the one for the measurement *sample rate* (48 kHz here) and the desired profile (`flat-48.0.txt`, `bk-48.0.txt`, `subultra-48.0.txt`, …) by editing `PSPointsFile` in `config.drc`.
- **Microphone**: in the default `config.drc` the microphone-compensation stage is **off** (`MCFilterType = N`, `MCNumPoints = 0`), so `wm-61a.txt` is not used as-is. If your microphone comes with a calibration curve (see [Measurement microphones](#measurement-microphones)), place it in that format, point `MCPointsFile` at it and enable the stage (`MCFilterType` to minimum/linear phase and `MCNumPoints` to the number of points) to discount the microphone's response.

#### From wav to raw

DRC only supports audio files in raw format, so the WAV files of the impulses must be converted. And afterwards, the filters generated by drc in raw format must be converted to WAV to make them compatible with NatAmbio. For this, pca4drc provides two very simple tools:
```
    python raw2wav.py <raw> [<raw> ...] [--rate 48000]
    python raw2wav.py impulses/*.raw --rate 48000
```
```
    python wav2raw.py <wav> [<wav> ...]
    python wav2raw.py impulses/*.wav
```

#### How to invoke it

The manual invocation of `drc` to obtain the DRC FIR filter for each loudspeaker is:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw config.drc
```

`--BCBaseDir` sets the measurements folder (so that the config's relative paths, such as `../target/...`, keep resolving) and `--BCInFile` the input impulse.

The filters resulting from the DRC process are two: `rps.raw` (standard filter) and `rms.raw` (the same filter in minimum phase). Both can be found in ``BCBaseDir``.

If you want to change the target of config.drc you can invoke it with:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw --PSPointsFile=new_target.txt config.drc
```

And if you want to include a microphone calibration file it is invoked with:

```sh
drc --BCBaseDir=Measurement_01/ --BCInFile=impulses/left.raw --MCFilterType=L --MCPointsFile=calibration.txt config.drc
```
Both options can be combined by including all the parameters.

## Manual process vs scripted process

Once the procedure for taking a measurement has been described, the process continues by repeating the steps for each of the different loudspeakers and, if measuring in multipoint mode, for different microphone positions. This requires keeping mental order with the names of the WAV files so they can be identified and/or organized into folders for each loudspeaker.

Keeping this order is easier if you use a helper script provided in tools/python_pca4drc named ``measure_pca4drc.sh``.

The script measure_pca4drc.sh allows controlling the execution of:

1. Log-sweep generation.
2. Running NatAmbio in the mode that corresponds to the type of NatAmbio set up.
3. Calibration of input and output gains.
4. The measurement process itself.
5. Quality control of the measurements.
6. The process of obtaining impulses by convolution.
7. Applying PCA4DRC if there is more than one measurement for each loudspeaker.
8. Generating filters via DRC.

Below, the application of the aforementioned script is explained for each possible NatAmbio system:

1. A single dipole without subwoofer.
2. A dipole with subwoofer.
3. Two dipoles without subwoofer.
4. Two dipoles with subwoofer.

And, in addition, with the possibility of measuring a single impulse per loudspeaker (without using PCA4DRC) or more than one impulse per loudspeaker, each measured at a different point within the target listening area.

### Preparing the measurement directory

It is advisable to run the process from a **new, dedicated measurement folder** (one per campaign), so that everything generated and edited is collected together and easy to track. But you must bear in mind that the script **does not look for all the auxiliary files in that folder**: it resolves paths from two different roots.

**Root 1 — `TOOLS_DIR`** (by default, the folder where the `.sh` lives, `tools/python_pca4drc/`). From there come, by default:

- The python scripts (`sweepgen.py`, `fft_convolve.py`, `pca4drc.py`, `check_capture.py`, `wav2raw.py`, `raw2wav.py`).
- The **natambio configuration XML** (`$TOOLS_DIR/half_natambio_measurements_normal.xml`, etc.).
- The **`config.drc`** (`$TOOLS_DIR/config.drc`).

**Root 2 — the current directory (CWD)**, that is, the measurement folder from which the command is launched. There the `sweep_48k.wav` / `inverse_48k.wav`, the output folders `m_<way>/`, `i_<way>/`, `i_<way>/pca4drc/`, and — importantly — the **target curve** `target/` are **created** (and looked for).

> ⚠️ **The `target/` is looked for in the measurement folder, not in `TOOLS_DIR`.** DRC is invoked with `--BCBaseDir=i_<way>/` (a path relative to the CWD) and in `config.drc` the curve is `PSPointsFile = ../target/48.0 kHz/...`; therefore it resolves as `i_<way>/../target/... → ./target/...` **of the measurement directory**. Even though a `target/` exists inside `tools/python_pca4drc/`, DRC does not look there: you must have `./target/` in the measurement folder or Phase 4 will fail.

#### What to copy (and what not)

| File | Where it is looked for by default | Copy to the measurement directory? |
|---|---|---|
| `measure_pca4drc.sh` + `.py` scripts | `TOOLS_DIR` (the script's folder) | **No** — they are not edited |
| natambio configuration XML | `TOOLS_DIR` | **Yes** — it must be edited (see [NatAmbio in bypass](#natambio-in-bypass-and-subwoofer-mode)) |
| `config.drc` | `TOOLS_DIR` | Yes, if you adjust the target or the microphone correction |
| `target/` folder | **CWD** (`./target/`) | **Yes, mandatory** for Phase 4 (DRC) |
| `sweep`/`inverse`, `m_*`/`i_*` outputs | CWD | Created automatically |

**It is not advisable to copy the `.sh` to the measurement folder**: since `TOOLS_DIR` is deduced from the location of the script itself, copying it there would stop it finding the `.py`, the XML and the `config.drc`. It is preferable to leave the scripts in their location (installed or in the repo) and point at the local copies of the editable files via environment variables (`NATAMBIO_CONFIG`, `DRC_CONFIG`).

#### Preparation and run example

The script **runs from its location in the toolkit, without copying it**: since it deduces `TOOLS_DIR` from the path it is invoked with, it is enough to define that path once and call it through it. This also tells it where the `.py`, the XML and the `config.drc` are by default.

```sh
# Path to the toolkit (once per session). With the tools installed — the usual
# case — (Debian package natambio-drc or `make install --prefix=/usr`):
export TOOLS_DIR=/usr/share/natambio/python_pca4drc
# If instead you work from a clone of the repository, it would be:
#   export TOOLS_DIR=<path_to_repo>/tools/python_pca4drc

mkdir measurement_2026-06-23 && cd measurement_2026-06-23

# Copy only what you are going to edit / what DRC looks for in the CWD:
cp "$TOOLS_DIR/half_natambio_measurements_normal.xml" .
cp "$TOOLS_DIR/config.drc" .
cp -r "$TOOLS_DIR/target" .

# (edit the XML here —the output destnames— and, if needed, config.drc)

# Run the script from the toolkit, pointing at your local copies:
NATAMBIO_CONFIG=./half_natambio_measurements_normal.xml \
DRC_CONFIG=./config.drc \
FULL_NATAMBIO=false NUM_POS=1 \
"$TOOLS_DIR/measure_pca4drc.sh"
```

This way **everything modified and generated stays inside `measurement_2026-06-23/`** (edited XML, `config.drc`, `target/`, sweeps, impulses and DRC filters), while the scripts remain intact in `$TOOLS_DIR`. The measurement folder is self-contained and tracking is trivial.

> In the examples that follow `"$TOOLS_DIR/measure_pca4drc.sh"` is used, assuming `TOOLS_DIR` is exported as above. The script is never copied to the measurement folder: doing so would break the detection of `TOOLS_DIR` and stop it finding the `.py`.

> If you want to enable the **microphone** compensation, `MCPointsFile = wm-61a.txt` (without `../`) would resolve as `i_<way>/wm-61a.txt`; to avoid copying it into each way folder, the cleanest is to give an **absolute path** in `MCPointsFile`.

#### Common case: tools installed from the Debian package

The previous example already uses the installed-package path (`TOOLS_DIR=/usr/share/natambio/python_pca4drc`), which is the **usual** one after installing the `natambio-drc` package (or `make install --prefix=/usr`). If instead you work from a clone of the repository, just point `TOOLS_DIR` at `<path_to_repo>/tools/python_pca4drc`; the rest of the flow is identical.

Full distribution of the files after installing the package:

| Content | Installed path |
|---|---|
| `measure_pca4drc.sh` + `.py` scripts | `/usr/share/natambio/python_pca4drc/` |
| Launchers for the standalone tools | `/usr/bin/natambio-sweepgen`, `natambio-fft-convolve`, `natambio-check-capture`, `natambio-pca4drc`, `natambio-wav2raw`, `natambio-raw2wav` |
| natambio configuration XML | `/usr/share/natambio/python_pca4drc/{half,full}_natambio_measurements_{normal,subwoofer}.xml` |
| `config.drc` (the flow's DRC config) | `/usr/share/natambio/python_pca4drc/config.drc` |
| Target curves | `/usr/share/natambio/python_pca4drc/target/<rate>/` |
| Reference DRC presets | `/usr/share/natambio/python_pca4drc/config/<rate>/<profile>.drc` |
| Microphone calibration curves | `/usr/share/natambio/python_pca4drc/mic/` (`ecm8000.txt`, `wm-60a.txt`, `wm-61a.txt`) |

Two details specific to the installation:

- **`/usr/share` is read-only** (owned by `root`): you cannot measure there. You must always work from a **measurement folder of your own with write permissions** (e.g. under `~`), copying the editable files into it, just as before.
- The standalone tools (`.py`) have their launcher in `/usr/bin`, but `measure_pca4drc.sh` does **not**: it is invoked by its path, `"$TOOLS_DIR/measure_pca4drc.sh"`.

The flow is identical to before, changing only `TOOLS_DIR`:

```sh
export TOOLS_DIR=/usr/share/natambio/python_pca4drc

mkdir -p ~/measurements/room_2026-06-23 && cd ~/measurements/room_2026-06-23

# Copy the editable files / what DRC looks for in the CWD:
cp "$TOOLS_DIR/half_natambio_measurements_normal.xml" .
cp "$TOOLS_DIR/config.drc" .
cp -r "$TOOLS_DIR/target" .

# (edit the XML here —the output destnames— and, if needed, config.drc)

# Run the installed script, pointing at your local copies:
NATAMBIO_CONFIG=./half_natambio_measurements_normal.xml \
DRC_CONFIG=./config.drc \
FULL_NATAMBIO=false NUM_POS=1 \
"$TOOLS_DIR/measure_pca4drc.sh"
```

> The **microphone** calibration curve is included in the package under `mic/` (e.g. `/usr/share/natambio/python_pca4drc/mic/ecm8000.txt` for the Behringer ECM 8000). To use it, give its **absolute path** in `MCPointsFile` inside `config.drc` and enable the microphone-compensation stage (see [Applying DRC-FIR](#applying-drc-fir)).

### Single dipole, no subwoofer, one measurement per channel

The simplest case is a basic stereo system that you want to turn into a single-dipole NatAmbio, applying DRC filters from a single measurement per channel.

These conditions translate into three inline parameters of [`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh), prefixed to the call:

- `FULL_NATAMBIO=false` → a single dipole: it measures only two ways, *front left* and *front right* (uses `half_natambio_measurements_normal.xml`).
- `SUBWOOFER=false` → without subwoofer. It is the default value, so it can be omitted.
- `NUM_POS=1` → a single measurement per channel. With a single measurement **no PCA is applied**: the measured impulse is used directly as DRC input.

The complete run (the five phases: sweep → measurement → impulses → DRC) is:

```sh
FULL_NATAMBIO=false NUM_POS=1 "$TOOLS_DIR/measure_pca4drc.sh"
```

Or, leaving `SUBWOOFER=false` explicit for clarity:

```sh
FULL_NATAMBIO=false SUBWOOFER=false NUM_POS=1 "$TOOLS_DIR/measure_pca4drc.sh"
```

It is advisable to **calibrate the levels beforehand** (following the process explained later in this document) and reuse the recommended gains in this call, e.g.:

```sh
FULL_NATAMBIO=false NUM_POS=1 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

It is required that, before starting the measurement process, jackd is active and configured for the desired audio interface. The script will run NatAmbio with the corresponding xml configuration, which in this case will be half_natambio_measurements_normal.xml. Therefore, before starting the measurements you must edit the xml file associated with NatAmbio to configure the correct audio outputs.

### Single dipole, with subwoofer, one measurement per channel

In this case, compared to the previous one, only one parameter changes in the script run:

```sh
FULL_NATAMBIO=false SUBWOOFER=true NUM_POS=1 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

The script will run natambio with the half_natambio_measurements_subwoofer.xml configuration. Before starting it, you will have to edit this xml to configure the low-pass and high-pass filters in the desired way.

### Single dipole, several measurements per channel

In this case it is only necessary to increase the value of NUM_POS from 1 to the desired number. For the case without subwoofer:

```sh
FULL_NATAMBIO=false SUBWOOFER=false NUM_POS=16 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

For the case with subwoofer:

```sh
FULL_NATAMBIO=false SUBWOOFER=true NUM_POS=16 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

### Two dipoles, several measurements per channel

In this case, the FULL_NATAMBIO parameter becomes ``true``, and in addition it is necessary to increase the value of NUM_POS from 1 to the desired number. For the case without subwoofer:

```sh
FULL_NATAMBIO=true SUBWOOFER=false NUM_POS=16 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

For the case with subwoofer:

```sh
FULL_NATAMBIO=true SUBWOOFER=true NUM_POS=16 GAIN_OUT=-3 GAIN_IN=12 "$TOOLS_DIR/measure_pca4drc.sh"
```

### Calibration

Before taking any good measurement it is advisable to fix playback (`GAIN_OUT`) and capture (`GAIN_IN`) gains that work **at the same time** for both ways of the dipole (front left and front right), without clipping, with sufficient level and good signal-to-noise ratio. For this the script [`measure_pca4drc.sh`](../tools/python_pca4drc/measure_pca4drc.sh) provides a **calibration mode** (`CALIBRATE=1`) that limits itself to playing the sweep and recording to adjust levels: it does not extract impulses, nor does PCA, nor DRC (it disables those phases so as not to require their dependencies).

For a **single-dipole, no-subwoofer** system, the mode is selected with:

- `FULL_NATAMBIO=false` → only two ways, *front left* and *front right* (one dipole).
- `SUBWOOFER=false` → system without subwoofer (it is the default value, can be omitted).
- `CALIBRATE=1` → gain calibration mode.

```sh
cd <working_directory>
FULL_NATAMBIO=false CALIBRATE=1 "$TOOLS_DIR/measure_pca4drc.sh"
```
When calibrating it is advisable to start with initial gains substantially lower than the defaults (`GAIN_OUT=0` dB, `GAIN_IN=10` dB), which can be done by likewise prefixing their variables to the call:

```sh
FULL_NATAMBIO=false CALIBRATE=1 GAIN_OUT=-10 GAIN_IN=5 "$TOOLS_DIR/measure_pca4drc.sh"
```

What the calibration mode does, step by step:

1. Generates the sweep and its inverse (Phase 0), unless skipped with `DO_SWEEP=0` reusing an existing pair.
2. Starts `natambio` with the `half_natambio_measurements_normal.xml` configuration (half system = one dipole, no subwoofer) and shows the **routing report** (which natambio output goes to each physical card output). You must confirm with Enter that the assignment is correct.
3. Plays the sweep through **each way** (front L and front R) at the gains defined at that moment, records the microphone capture and analyses it with `check_capture.py`, warning about **clipping**, **low level** (`MIN_LEVEL`, −40 dBFS by default) or **low SNR** (`MIN_SNR`, 20 dB).
4. After trying both ways, if any does not meet the requirements, you can repeat the process with **two new values of** `GAIN_OUT GAIN_IN` (e.g. `-3 12`), as well as adjusting the physical gain of the microphone preamp. When both ways give correct levels, you just press Enter to accept.
5. When finished, natambio stops and the script prints the **recommended gains**, ready to use in the actual measurement, for example:

   ```sh
   GAIN_OUT=-3 GAIN_IN=12 FULL_NATAMBIO=false "$TOOLS_DIR/measure_pca4drc.sh"
   ```

> During calibration it is advisable to adjust the **physical** gain of the microphone preamp first and leave the fine adjustment to `GAIN_OUT`/`GAIN_IN`. And to watch the playback level at all times to avoid accidents (see the warning in [Before measuring](#before-measuring)).

The script repeats the whole process for each **way** (loudspeaker) of the system:

- **Full NatAmbio** (`FULL_NATAMBIO=true`, default): four ways — `front_left`, `front_right`, `rear_left`, `rear_right`.
- **Two-loudspeaker system** (`FULL_NATAMBIO=false`): only `front_left` and `front_right`.


## Generated directory structure

Running the script in a working directory creates (for full NatAmbio):

```
m_front_left/   m_front_right/   m_rear_left/   m_rear_right/    # recorded sweeps (Phase 1)
i_front_left/   i_front_right/   i_rear_left/   i_rear_right/    # impulses (Phase 2)
    └── pca4drc/    PCA_0.wav, PCA_1.wav, …  +  PCA_0.raw, …     # PCA components (Phase 3)
    └── rps.raw, rms.raw  (+ their .wav)                        # DRC filters (Phase 4)
```

The rps.wav and rms.wav files of each loudspeaker are the ones applicable to NatAmbio. In [NatAmbio configuration examples](config_samples/README.md) you can find how to incorporate them.

## Usage and configuration

In case at some point you want to change some specific behavior or variable of the ``measure_pca4drc.sh`` script that has not been covered so far in this document, here is the list of possibilities. All variables have a default value, but can be **overridden on the fly** by prefixing them to the call (without editing the script):

```sh
"$TOOLS_DIR/measure_pca4drc.sh"                       # the five phases, interactive (4 ways, normal)
FULL_NATAMBIO=false "$TOOLS_DIR/measure_pca4drc.sh"   # 2-loudspeaker system (front L/R only)
SUBWOOFER=true "$TOOLS_DIR/measure_pca4drc.sh"        # start natambio with the subwoofer config
NUM_POS=8 "$TOOLS_DIR/measure_pca4drc.sh"             # 8 microphone positions instead of 16
AUTO=1 "$TOOLS_DIR/measure_pca4drc.sh"                # no interactive pauses
DO_SWEEP=0 "$TOOLS_DIR/measure_pca4drc.sh"            # use an existing sweep/inverse
DO_MEASURE=0 "$TOOLS_DIR/measure_pca4drc.sh"          # re-process what was already measured (skip measurement)
DO_DRC=0 "$TOOLS_DIR/measure_pca4drc.sh"             # everything except the DRC correction
DO_MEASURE=0 DO_IMPULSES=0 DO_PCA=0 "$TOOLS_DIR/measure_pca4drc.sh"  # only DRC over already-generated PCA_0.raw
```

The phase switches `DO_SWEEP` / `DO_MEASURE` / `DO_IMPULSES` / `DO_PCA` / `DO_DRC` are `1` (enabled) or `0` (skipped) and are independent, so they can be combined to run only the phases of interest.

Most common variables:

| Variable | Default | Meaning |
|---|---|---|
| `FULL_NATAMBIO` | `true` | `true` = 4 ways (front+rear); `false` = 2 (front) |
| `SUBWOOFER` | `false` | natambio config with/without subwoofer |
| `NUM_POS` | `16` | Number of microphone positions |
| `IN_MEAS` | `system:capture_1` | Microphone capture JACK port |
| `SELECT_INPUT` | `0` | `1` = choose `IN_MEAS` via interactive menu before measuring |
| `GAIN_OUT` / `GAIN_IN` | `0.0` / `10.0` dB | Playback / capture gain |
| `REC_SECONDS` | `10` | Duration of each capture (s) |
| `MIN_LEVEL` / `MIN_SNR` | `-40` dBFS / `20` dB | Capture validation thresholds |
| `OUTPUT_LEN` | `131072` | Length of the PCA components (samples) |
| `PCA_NORMALIZE` | `true` | Normalize the components to the peak of the principal one |
| `DRC_CONFIG` | `config.drc` | DRC-FIR configuration |
| `AUTO` | `0` | `1` = no interactive pauses |

The complete list of variables is documented in [`tools/python_pca4drc/README.md`](../tools/python_pca4drc/README.md). In the examples above `"$TOOLS_DIR/measure_pca4drc.sh"` is invoked assuming `TOOLS_DIR` has been exported pointing at the toolkit (e.g. `export TOOLS_DIR=/usr/share/natambio/python_pca4drc`); the script reads that same variable to locate the `.py`, the XML and the `config.drc`, so it never needs to be copied to the measurement folder.

## Recommended workflow

1. **Prepare the room and the system**: JACK running at 48 kHz, microphone placed at the first position, reasonable preamp levels.
2. **First full, interactive run**: `"$TOOLS_DIR/measure_pca4drc.sh"`. Review the configuration report (way routing) before confirming.
3. **Adjust the gain** if `check_capture.py` warns about level/SNR, and repeat the position.
4. Once all positions and ways have been measured, phases 2–4 generate impulses, PCA and DRC filters with no further intervention.
5. To **re-process** without measuring again (e.g. trying another target curve or PCA parameters), repeat with `DO_SWEEP=0 DO_MEASURE=0`.
