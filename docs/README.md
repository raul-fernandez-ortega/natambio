# Nat(ural) Ambio(phonics): NatAmbio

*Also available in: [Español](README_es.md)*

***The space is already in the recording. NatAmbio simply makes it audible.***

NatAmbio is a spatial playback system designed for listening to conventional stereo recordings through real-time digital processing. It is suited to both professional and, above all, domestic environments, since its implementation cost is not high. In fact, its development took place in a private home, in a standard multi-purpose living room.

NatAmbio pursues a straightforward goal: to reproduce conventional stereo recordings while simultaneously broadening and focusing the frontal scene of the performance, and projecting the ambient information contained in the recording into a non-localised sound field around the listener. To do so, NatAmbio extracts the ambient information already present in the original stereo recording, creating an enveloping experience without resorting to specific multichannel formats or artificial spatial effects.

Unlike conventional multichannel systems, NatAmbio requires no specific recordings and generates no artificial ambient channels. All reproduced spatial information comes exclusively from the original stereo signal.

There are two system layouts. The single stereo dipole:

![One dipole NatAmbio](figs/ambio_one_dipole.svg)

And the dual stereo dipole — one frontal, one rear ambient:

![Two dipole NatAmbio](figs/ambio_two_dipole_v2.svg)

In either case, one or more subwoofers can be added to the system:

![Two dipole NatAmbio subwoofer](figs/ambio_two_dipole_sub.svg)

The first layout, based on a single frontal ambiopole, already delivers most of the spatial perception that NatAmbio provides. The second ambiopole offers a further small step in the ambient sensation inherent to the recording. Whether to add a subwoofer depends on the bass response sought and the type of front loudspeakers in the system (e.g. small monitors with very good dispersion, supported by a subwoofer for frequencies below 100 Hz).

NatAmbio extracts the ambient signal from the stereo recording itself — it does not generate it through some purpose-built effect. For example, from a monophonic recording (two identical channels) there is no spatial ambience; NatAmbio reproduces such recordings with full focus at the centre of the scene.

It is in recordings with natural ambience — live performances, acoustic music in special environments (notably acoustic jazz and pop, and very notably orchestral classical music and opera) — where NatAmbio tends to deliver the most striking results, projecting the natural ambience captured in the recording into a non-localised, fully ambient sound field. NatAmbio allows this sensation to be adjusted to taste, making playback drier or more enveloping.

For commercial pop music, where artificial processing (echoes, reverb, panning, etc.) is commonly used to create the necessary sense of space, NatAmbio also detects and extracts this less natural ambience, placing it in an indeterminate sound field around the main sonic scene.

Throughout, NatAmbio maintains frontal focus on the music; additionally, by applying a crosstalk cancellation (XTC) algorithm to each dipole, it widens the frontal scene well beyond the loudspeaker positions, in the manner of [Ambiophonics](https://en.wikipedia.org/wiki/Ambiophonics). This frontal scene widening is also highly configurable.

In addition to ambient scene generation — projected into an indeterminate sound field — and XTC filtering on both dipoles, NatAmbio includes a convolver whose engine is the [zita-convolver](https://kokkinizita.linuxaudio.org/linuxaudio/) library, enabling [DRC](https://drc-fir.sourceforge.net/) equalisation, subwoofer management where applicable, and a configurable loudness effect on either dipole.

## NatAmbio is the system and the software that shapes it

At the core of a NatAmbio system is its software, which bears the same name. The NatAmbio software has been designed specifically for the playback architecture described in this document. Both elements form a coherent whole and are conceived to be used together.

As software, NatAmbio is a C/C++ programme licensed under [GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html#license-text) for GNU/Linux systems. It therefore runs on a GNU/Linux computer at the heart of the system. That computer executes various DSP algorithms responsible for:

* extracting the main and ambient components of the stereo recording, allowing on-demand adjustment of the balance between those components before generating NatAmbio's characteristic spatial scene,
* generating the aforementioned crosstalk cancellation (XTC) filters by convolving them with the recorded signal,
* equalising the stereo dipoles and managing the routing of the main signal to the subwoofer where needed, also by convolution,
* and constructing the spatial sound field reproduced by the loudspeakers, sending different combinations of the main and ambient components to each dipole.

## What is a NatAmbio system?

A NatAmbio system consists of two inseparable elements:

### 1. A specific loudspeaker arrangement

NatAmbio adopts the [PanAmbio](https://www.filmaker.com/papers/SMPTE144-Compatible.pdf) architecture proposed by Robin Miller, based on two Ambiophonics stereo dipoles<sup>[1](#note-ambiopole)</sup>: one frontal and one rear. It is also fully applicable, as mentioned above, to a single-dipole frontal system.

Each dipole uses crosstalk cancellation (XTC) to widen the perceived sound stage. The system can operate with a single frontal dipole, although it reaches its fullest expression with two dipoles and one or more subwoofers.

NatAmbio can equalise both dipoles (and any additional subwoofer(s)) through DRC. This requires generating DRC filters using [DRC-FIR](https://drc-fir.sourceforge.net/) by Denis Sbragion, another GPL-compatible GNU/Linux tool. NatAmbio's documentation includes a brief guide for this, and the project provides simple but practical scripts for taking the necessary acoustic measurements and deriving the equalisation filters from them.

### 2. The NatAmbio DSP processor

NatAmbio is also the name of the software that performs the digital processing needed to drive those dipoles.

The software continuously analyses the stereo input signal, extracts its spatial information, and generates the signals required by each loudspeaker in the system.

The result is an enveloping sound field derived from the original stereo recording itself, whose breadth and spatial content depend directly on the acoustic characteristics present in that recording.

It is worth stressing once more that all functionality NatAmbio performs through convolution — XTC, DRC, loudness, subwoofer routing — is made possible by the [zita-convolver](https://kokkinizita.linuxaudio.org/linuxaudio/) library by Fons Adriaensen. Zita-convolver is likewise a GPL-compatible GNU/Linux utility.

NatAmbio is Free Software, licensed under GPLv3, which permits redistribution and modification of its source code under the terms of the GPLv3 itself.

## Documentation (links pending)

NatAmbio offers varied documentation depending on how you want to approach it. The technical articles develop the models on which NatAmbio is based — those that are most original or novel. The usage documentation covers the NatAmbio software itself.

A series of configuration examples and guides is also included, together with hardware options for building the core of a NatAmbio system.

Beyond the technical and usage documentation, a set of articles of a more accessible character explore some psychoacoustic implications of stereo playback and the ambient extraction used by NatAmbio.

Finally, a commented selection of recordings that proved especially relevant during the development and evaluation of the system is included.

### Technical articles

- [NatAmbio Ambient Extractor (NAE)](nae/nae_en.md): an algorithm for extracting the ambient traces of a stereo recording.
- [Design of a stereo crosstalk canceller (XTC) by convolution for NatAmbio](xtc/xtc_filters_en.md).
- [Application of PCA to impulsive acoustic measurements of loudspeakers](pca4drc/pca4drc_en.md).

### NatAmbio usage documentation

- Requirements, download, compilation and installation.
- NatAmbio configuration.
- [Example configurations](config_samples/README.md).
- [Running natambio as an automatic service on a standalone NatAmbio DSP processor](../natambio_as_a_service/natambio_systemd.md).
- How to build an audio system that is a NatAmbio system.

### Other available tools

- [python_nae_natambio](../tools/python_nae_natambio/README.md) Python script for offline NAE processing of WAV files. Enables specific testing of the NAE algorithms. Generates analytical plots like those shown in [NatAmbio Ambient Extractor (NAE)](nae/nae_en.md).
- [ladspa_nae_natambio](../tools/ladspa_nae_natambio/README.md) NAE module for LADSPA-compatible audio hosts (applyplugin, ecasound, …).
- [xtc_filters](../tools/xtc_filters/README.md) A C tool that implements the [design of a stereo crosstalk canceller (XTC) by convolution for NatAmbio](xtc/xtc_filters_en.md) for standalone use outside natambio. Generates the XTC FIR filters for use in another convolver.
- [pca4drc](../tools/python_pca4drc/README.md) A set of tools to take acoustic room measurements, apply the PCA4DRC method and obtain a reference impulse for use in a DRC FIR filter generator (http://drc-fir.sourceforge.net recommended).

### Technical intuitions on future possibilities of stereo

### Audiophile corner: the recordings that made NatAmbio possible

### Personal notes

#### Notes

[1]<a id="note-ambiopole"></a> A stereo dipole is a sound source in an Ambiophonic system, made by two closely spaced loudspeakers that ideally span 10 to 30 degrees. Thanks to the cross-talk cancellation method, a stereo dipole can render an acoustic stereo image nearly 180 degrees wide (single stereo dipole) or 360 degrees (dual or double stereo dipole).
