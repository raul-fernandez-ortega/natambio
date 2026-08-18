# Annex — Relationship with the Literature

### Companion to *Design of a Convolution-Based Stereo Crosstalk Canceller (XTC) for NatAmbio*

*Also available in: [Español](xtc_filters_anexo_es.md)*

---

## Purpose of this annex

The main text develops the model in a self-contained way, starting from the iterative analysis of
the successive cancellations and arriving at the filters $F^{direct}$ and $F^{cross}$. That
development was carried out independently, which is why the main text does not lean on external
references in order to proceed.

This annex exists to identify **which parts of that development coincide with already published
results, which are adaptations, and which are proper to this work**. It adds nothing to the model:
its purpose is to let a reader who wants to go to the primary sources know which ones to go to,
and why.

It has been written separately precisely so as not to interrupt the thread of the main text, whose
intended reader is the advanced enthusiast who wants to understand and build the system, not
follow a bibliographical discussion.

---

## A.1 · The recursive structure and the power series

The iterative development of the main text —the left loudspeaker emits, the pulse contaminates the
right ear, the right loudspeaker emits an anti-pulse, which in turn contaminates the left ear, and
so on— **is a known result**, and the series it leads to has been published.

**Origin of the recursion.** Atal, Hill and Schroeder (1966) were the first to recognize that the
solution to the crosstalk cancellation problem is intrinsically recursive.

**The power series.** Kirkeby, Nelson and Hamada (1998), in their analysis of the *stereo dipole*
under free-field conditions, solve the system and expand the result in exactly this way. Their
equation (13):

```math
\frac{1}{1-z} = \sum_{n=0}^{\infty} z^{n}, \qquad |z| < 1
```

and their equation (15), in the time domain:

```math
\begin{bmatrix} v_1(t) \\ v_2(t) \end{bmatrix}
= r_1 \begin{bmatrix} -g_c\, d(t-\tau_c) \\[2pt] d(t) \end{bmatrix}
\ast \sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

A train of positive pulses from one loudspeaker and negative ones from the other, at even and odd
multiples of the delay. **This is, term by term, the pair of filters of the main text:**

```math
F^{direct} = \delta + \sum_{i=1}^{N} G^{2i}
\quad \Longleftrightarrow \quad
\sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

```math
F^{cross} = -\sum_{i=1}^{N} G^{2i-1}
\quad \Longleftrightarrow \quad
-g_c\, d(t-\tau_c) \ast \sum_{n=0}^{\infty} g_c^{\,2n}\, \delta\!\left(t - 2n\,\tau_c\right)
```

Their physical interpretation (§2.1.1 of their article) also coincides, sentence by sentence, with
the iterative development of this document.

**Consequence for reading the main text:** the equations for $F^{direct}$ and $F^{cross}$ are not
an original contribution of NatAmbio. What is proper to it is what is done with them —see A.6, A.7
and A.11.

**The convergence condition** $|G| < 1$ is the same as their $|z| < 1$ in equation (13).

---

## A.2 · The definitions of G and of the delay

**The same definition.** Their equations (1) and (2) define

```math
g_c = \frac{r_1}{r_2}, \qquad \tau_c = \frac{r_2 - r_1}{c_0}
```

with $r_1$ the direct path and $r_2$ the crossed one.

The $G = H_{cross}/H_{direct}$ of the main text is the **general form** of that same ratio: the
quotient between the cross and the direct transfer function. Their $g_c$ is that ratio **evaluated
under free-field conditions**, where the transfer functions are monopoles with $1/r$ decay and the
quotient therefore reduces to the ratio of distances.

**The substantive difference lies in how it is populated.** They use a free-field model with two
point sources and two microphones, **with no head present**: the cross attenuation arises solely
from spherical spreading over unequal distances. With the geometry of this document that would
give

```math
\Delta r \approx \Delta M \sin\Theta = 0.18 \cdot \sin 20^{\circ} = 61.6\ \text{mm}
```

```math
g_c = \frac{1.969}{2.031} = 0.970
\quad \Longrightarrow \quad
\text{ILD} \approx 0.27\ \text{dB}
```

as against the **10 dB** used by NatAmbio, derived from the acoustic shadow of the head on the
basis of public HRTF datasets. A factor of roughly 36 in amplitude.

The two models do not contradict each other: they measure different things. But the difference has
a direct methodological consequence, taken up in A.8.

**A note on angular convention.** Their $\theta$ is the **total** angle between loudspeakers; the
$\Theta$ of this document is **half** of it. The 20° here are the 40° there. Once the conventions
are reconciled, their equation (17),

```math
\tau \approx \frac{\Delta M}{c_0}\,\sin\!\left(\frac{\theta}{2}\right)
\quad \Longrightarrow \quad
\tau(\theta = 40^{\circ}) = 179.5\ \mu\text{s}
```

agrees with the **180 µs** used here to within 0.3 %. This is to be expected: the ITD is geometry,
and both routes —theirs from distances, this document's from HRTF models— converge on the same
value.

---

## A.3 · A sixty-year-old technology, absent from commercial audio

The review above brings something into view that deserves comment, because it explains the tone in
which the main text is written.

**The chronology.** The principle is patented in **1966** (Atal, Hill and Schroeder). It is
demonstrated experimentally with loudspeakers in an anechoic chamber in **1971** (Damaske). Its
effect is measured, and the physical-barrier alternative tested, in **1986** (Bock and Keele). The
theory matures between 1989 and 1998: sum/difference structure (Cooper and Bauck), adaptive inverse
filters (Nelson, Hamada and Elliott), the *stereo dipole* and regularized deconvolution (Kirkeby,
Nelson, Hamada and Orduña-Bustamante). It reaches the domestic sphere with Ambiophonics from
**2001** onwards, and with Choueiri's BACCH filters around **2008**.

And from there, public development largely stops.

**The computational explanation only covers the first half.** It is true that the 1966 analogue
implementation was limited, and that convolution with long FIR filters was not viable in real time
until well into the 1990s. But a 4096-tap stereo convolution has been trivial for any processor for
the past twenty years. **The technology became cheap precisely when the industry stopped pursuing
it**, so a lack of computing power does not account for its subsequent absence.

**Plausible reasons**, stated as such and with no pretension of being a thesis:

- **The listening position.** XTC requires a fixed position and reasonable symmetry. Consumer audio
  moved in the opposite direction: several listeners, informal positions, soundbars.
- **The individuality of the HRTF.** A generic filter is always a compromise, and the industry's
  answer to that problem was the headphone with a personalized HRTF, not the loudspeaker.
- **Coloration.** This is the historical criticism of XTC and the reason regularization exists.
  Processing that improves space at the expense of timbre is a hard sell.
- **Headphones solve the problem by construction.** There is no crosstalk to cancel, so the effort
  in binaural reproduction went there.
- **And for loudspeakers, the industry chose the opposite architecture**: more channels (5.1, 7.1,
  objects) rather than better control of two.

**Where it survives.** To say it has disappeared would be an overstatement: it persists in
commercial niches —the BACCH filters—, in automotive audio, in degenerate forms inside
"stereo widening" processors, and above all in amateur practice. But it is a fact that the
published research on XTC is, for the most part, earlier than 2010.

That a principle demonstrated half a century ago, today computationally cheap and with a large and
easily verifiable perceptual effect, should still be enthusiast territory is an anomaly. And it is,
to a considerable extent, the reason this project exists and is published.

---

## A.4 · The ringing frequency

Their equation (18) introduces a concept that this document handles implicitly when it speaks of
the comb ripple of the direct filter, but never names:

```math
f_0 \approx \frac{c_0}{\Delta M \cdot \theta}
\qquad\qquad
\text{rule of thumb:}\quad f_0 \approx \frac{100\ \text{kHz}}{\theta\,[^{\circ}]}
```

It is the spectral periodicity of the pulse train, equivalent to

```math
f_0 = \frac{1}{2\cdot\text{ITD}}
```

With the geometry of this document ($\theta = 40^{\circ}$ total, ITD = 180 µs) this gives
$f_0 \approx 2.8$ kHz.

**That is precisely the ripple that the main text keeps within the ±2 dB band.** The term and the
formula can be adopted as they stand, and they name a quantity that is already being controlled.

**Two different levers on the same coloration.** Their conclusion is that a total span of 10° is a
good choice because it **raises $f_0$ above 10 kHz**, taking the ripple out of the audible band, at
the price of demanding a greater low-frequency boost. NatAmbio works at 40° total, with $f_0$ well
inside the audible band, and attacks the problem from the other side: **by reducing $|G|$** —the
ILD offset described in A.7—, which lowers the *amplitude* of the ripple rather than moving its
*frequency* out of band.

The two levers are orthogonal and, in principle, combinable.

---

## A.5 · Low frequencies: ill-conditioning and limiting

The limiting of the cross filter below 200 Hz with a 6 dB/octave roll-off described in the main
text responds to a documented problem. Kirkeby, Nelson and Hamada (§3) state it thus:

> At low frequencies, the crosstalk cancellation problem is ill-conditioned. Consequently, each of
> the filters in the crosstalk cancellation network is likely to boost low frequencies by 30 dB or
> more.

And in their conclusions they note that reducing the loudspeaker span aggravates that boost, which
constitutes the practical limit of the *stereo dipole* strategy.

**The difference lies in where the remedy is applied.** They treat it on the **inversion** side, by
means of least-squares regularization (Kirkeby, Nelson, Hamada and Orduña-Bustamante, 1998).
NatAmbio treats it on the **model** side: by spectrally limiting the function $G$ itself before it
enters the series. It is a simpler solution and, for this case, sufficient.

---

## A.6 · Non-minimum phase with real HRTFs

This is the point at which the development of NatAmbio answers a difficulty that the literature
leaves open. Kirkeby, Nelson and Hamada, §3, first paragraph:

> When the free-field transfer functions are replaced by more realistic head-related transfer
> functions (HRTFs), it becomes necessary to consider the problem of inverting an ill-conditioned
> system that contains non-minimum-phase components.

From there they refer the reader to their FIR filter design methods, abandoning the series.

**The contribution of this document is not to abandon it.** The series is retained and populated
with HRTF data, sidestepping the non-minimum-phase problem by way of a modeling decision: instead
of measured HRTFs, a **monotonic, minimum-phase parametric model** of the ILD spectrum is used,
deliberately free of peaks and notches, precisely because their placement varies greatly with
individual anatomy.

That decision also means the filter introduces no appreciable group delay, something the
deconvolution methods of the literature do incur: they introduce a modeling delay of the order of
half the filter length.

---

## A.7 · The ILD offset as a regularization parameter

The main text reports an empirical adjustment: setting the ILD some **4 dB above** the natural HRTF
value gives a better balance, because it produces a gentler cross filter and reduces the comb
ripple, trading some spatial image for less coloration.

It is worth making explicit that **this is functionally a regularization parameter**. Within the
framework of Kirkeby, Nelson, Hamada and Orduña-Bustamante (1998), Tikhonov regularization
administers exactly that trade-off: cancellation depth against filter effort and coloration. The
control is the same; the route taken to reach it is different —here, listening; there, an
optimization criterion.

Acknowledging this does not diminish the adjustment: it situates it. And it suggests that the
optimal value could, in principle, be derived from a criterion rather than tuned by ear, something
this work has not attempted.

---

## A.8 · Truncation: N = 3–4 in the text, 16 iterations in the code

The main text justifies $N = 3\text{–}4$ for an ILD of 10 dB: each increment of $i$ reduces the
term by about 20 dB, so that the fourth is already on the order of −70 dB.

The implementation, however, runs **16 iterations**. This is not a discrepancy but a robustness
decision: the filter generator does not know *a priori* which ILD the user will configure, and the
number of terms required depends on it. Since term $i$ of the direct filter decays as
$`2i\cdot\text{ILD}_{dB}`$, the number of iterations needed to reach a −80 dB floor is

```math
i \approx \frac{40}{\text{ILD}_{dB}}
```

| Configured ILD | Iterations required |
|---|---|
| 20 dB | 2 |
| 10 dB | 4 |
| 5 dB | 8 |
| **2.5 dB** | **16** |

> **The 16 iterations cover any ILD above roughly 2.5 dB, leaving the last retained term below
> −80 dB.** With the usual setting of 10 dB the terms beyond the fourth are irrelevant, but the
> ladder is computed in full in case the configuration is lowered.

One further limit is worth noting: the ladder begins at the delay
$(\text{NSTEPS}-1)\cdot\text{ITD}$, so the filter length must exceed that value or the high-order
terms will be truncated by length before they are truncated by level. The generator warns when
this occurs.

---

## A.9 · The sum/difference structure

The matrix formulation of the main text —the symmetric plant
$`\mathbf{H} = H_{direct}\,\mathbf{C}_G`$ and its inverse— has a property that is not developed
there and is worth knowing: the matrix $\mathbf{C}_G$ and its inverse **share their eigenvectors**,
with reciprocal eigenvalues:

```math
\mathbf{C}_G = \begin{bmatrix} 1 & G \\ G & 1 \end{bmatrix}
\qquad
\mathbf{u}_1 = \begin{bmatrix} 1 \\ 1 \end{bmatrix},\;
\mathbf{u}_2 = \begin{bmatrix} 1 \\ -1 \end{bmatrix}
\qquad
\lambda_{1,2} = 1 \pm G
```

Consequently, the design of a symmetric canceller can be posed as **two independent scalar
filters**

```math
\frac{1}{1+G} \qquad \text{and} \qquad \frac{1}{1-G}
```

rather than as the inversion of a $2\times 2$ matrix. This is the structure known as the
*shuffler*, whose application to the transaural problem is developed by Cooper and Bauck (1989),
and whose origin goes back to Blumlein's sum/difference processing (1931).

This reading explains immediately why the entire low-frequency problem of A.5 lives in a single
scalar: it is the eigenvalue $1-G$, which tends to zero as $|G| \to 1$.

And it has a consequence that connects to A.10: **the sign of a component's inter-channel
correlation determines which of the two eigenvalues governs its passage through the system.** For a
component whose channels stand in the relation $r = \rho\, l$, decomposition in the eigenbasis
gives

```math
\begin{bmatrix} 1 \\ \rho \end{bmatrix}
= \underbrace{\frac{1+\rho}{2}}_{\text{symmetric weight}} \begin{bmatrix} 1 \\ 1 \end{bmatrix}
+ \underbrace{\frac{1-\rho}{2}}_{\text{antisymmetric weight}} \begin{bmatrix} 1 \\ -1 \end{bmatrix}
```

so that for $\rho > 0$ the symmetric weight dominates, and for $\rho < 0$ the antisymmetric one.

---

## A.10 · Synthetic ambience versus extracted ambience

This is the point at which NatAmbio parts company with the tradition it comes from, and it is worth
saying so plainly, because the architecture is the same.

### What Ambiophonics proposes

Glasgal (2001) describes the complete ambiophonic chain: player → room and loudspeaker correction →
crosstalk canceller → frontal ambiopole, plus an ambience path to the surround loudspeakers.
Compared with the system architecture figure of NatAmbio, **it is the same chain**.

The difference lies in how that ambience path is generated. Ambiophonics produces it by
**convolving the direct sound with the impulse response of a real hall**, measured before or after
the recording session, or taken from a library of great auditoria. This is the device Glasgal calls
the *Ambiovolver*.

His arguments for doing it this way are reasoned and sound within his framework: there is no need
to capture the hall response over and over for each work; the number and placement of the surround
loudspeakers cease to be critical; the recording engineer is freed from the compromise between the
perspective of the main pickup and the capture of ambience; and the listener can even choose which
hall to listen in.

**The resulting ambience, therefore, was not in the recording.** It comes from the impulse response
of another hall, and is synthesized at playback.

### What NAE does

NatAmbio starts from the opposite constraint: **not to create spatial information that is not in
the recording**. NAE has no hall library, does not convolve with foreign impulse responses and does
not generate reverberation. It decomposes the stereo signal into two orthogonal components and
routes the secondary one to the rear dipole.

Within that scheme the roles fall out as follows:

> **NAE is an enhancer of the natural ambience the recording already contains. XTC is the projector
> that carries it to a virtual position.**

Neither of the two creates the ambience: one separates it and the other places it.

### The same axis, twice over

What is notable is that this is **exactly the same divergence** that separates NatAmbio from the
primary-ambient extraction literature, where the ambience is obtained by decorrelation —that is, by
synthesizing material that was not there— in order to distribute it across several loudspeakers.

| Approach | Origin of the ambience |
|---|---|
| Ambiophonics | **creates** it by convolution with the impulse response of a real hall |
| PAE literature | **creates** it by artificial decorrelation |
| **NatAmbio** | **does not create it**: extracts what is already there and projects it |

The design constraint is therefore the **single distinguishing axis of the project**, and it
operates in both directions. Against the PAE literature it might be read as a difference of
destination —they feed an array, NatAmbio a dipole—; against Ambiophonics, with the same
architecture, the same domestic aim and the same canceller, it is a difference of principle.

### The trade-off, stated without adornment

The advantage of Ambiophonics is real and should be acknowledged: **its ambience is always
available and always good**, because it comes from a hall that has been chosen. NatAmbio's is
whatever the recording contains, with all its variability —and from a monophonic recording it
obtains none, as the main text itself points out.

What is gained in exchange is that the result bears a verifiable relationship to the original
material: the reproduced scene derives from the spatial information the recording engineer
captured, not from a playback decision.

### What remains open

The connection between NAE and the rest of the chain rests on a relationship that **has not been
robustly analyzed**: the one between a component's degree of correlation and its virtual
localization under crosstalk cancellation.

What is known today, and is recorded in the NAE documentation:

- The two extremes are structural: a $[1,\,1]$ signal with zero level difference is perceived as
  maximally localized and centered, and a $[1,\,-1]$ signal with zero level difference as maximally
  delocalized.
- Between them, laterality is **not monotonic**: it exhibits a maximum with anti-correlated content
  and an inter-channel level difference of the order of 8 to 9 dB.
- The position of that maximum does not shift when either the ILD or the ITD of the filter is
  varied, within the resolution of the measurement.

What is **not** known:

- Why the perceptual maxima fall where they do. There are hypotheses that fit —related to the band
  in which the filter is neutral in magnitude, and to the saturation of level-based lateralization—
  but none has been verified.
- The quantification, in degrees, of the aperture reached at each point.
- Whether the observations generalize beyond one listener and one room.

This is the piece that would connect the NAE decomposition quantitatively to the XTC → DRC chain,
and as of today it is open. All the measurements cited were obtained with tools included in the
public repository, so they are reproducible by anyone wishing to contrast, extend or refute them.

---

## A.11 · What is proper to NatAmbio

After the comparison above, what is not found in the literature consulted:

1. **Applying the ILD spectrum once per round trip** ($S^{i}$ instead of $S^{2i-1}$). This is a
   modeling decision that can only be posed if an explicit series exists, and it has no equivalent
   in the direct-inversion formulations.
2. **The XTC / DRC separation stated as an architectural principle.** In a free-field model the
   question does not arise, because $H_{direct}$ is trivial —a delay and a $1/r$ decay—; it appears
   only once $H_{direct}$ is a real measured response, with deep zeros, non-minimum-phase
   components and listener-position dependence.
3. **The monotonic, minimum-phase parametric model of the ILD spectrum**, fitted to the average of
   five public HRTF datasets, together with its explicit justification: avoiding peaks and notches
   whose placement depends on individual anatomy.
4. **Retaining the series with HRTF data**, which is the route the literature declares difficult
   and abandons (A.6).
5. **Extracting the ambience instead of synthesizing it** (A.10), which separates NatAmbio from
   both Ambiophonics and the PAE literature.

---

## A.12 · Practical requirements drawn from the literature

Kirkeby, Nelson and Hamada (§3) set out a hardware condition that deserves a place among the
system's assembly requirements:

> It is very important that the two loudspeakers have almost identical frequency responses (not
> just their amplitude responses, but also their phase responses must be the same). As a rule of
> thumb, "pair matching" to within ±0.5 dB in amplitude and ±5° in phase is more than sufficient
> to ensure accurate and symmetric imaging.

It is a demanding condition. NatAmbio's DRC stage addresses it in part —it corrects the response of
each path— but it is worth pointing out that phase matching between cabinets is a prior
requirement that equalization does not guarantee on its own.

---

## A.13 · Primary references

The following are additional to those already cited in the main text.

**Crosstalk cancellation: origin and formulation**

15. Atal, B. S., Hill, M. & Schroeder, M. R. (1966). *Apparent Sound Source Translator*.
    U.S. Patent 3,236,949. — Origin of the recognition that the solution is recursive in nature.
16. Kirkeby, O., Nelson, P. A. & Hamada, H. (1998). The "Stereo Dipole" — A Virtual Source Imaging
    System Using Two Closely Spaced Loudspeakers. *J. Audio Eng. Soc.* 46(5), 387–395. — Power
    series (eqs. 13–15), definitions of $g_c$ and $\tau_c$ (eqs. 1–2), ringing frequency (eq. 18),
    and statement of the real-HRTF problem (§3).
17. Cooper, D. H. & Bauck, J. L. (1989). Prospects for Transaural Recording. *J. Audio Eng. Soc.*
    37(1/2), 3–19. — Sum/difference (*shuffler*) structure applied to the transaural problem.
18. Bauck, J. & Cooper, D. H. (1996). Generalized Transaural Stereo and Applications.
    *J. Audio Eng. Soc.* 44(9), 683–705.

**Regularization and inversion**

19. Kirkeby, O., Nelson, P. A., Hamada, H. & Orduña-Bustamante, F. (1998). Fast Deconvolution of
    Multichannel Systems Using Regularization. *IEEE Trans. Speech and Audio Processing* 6(2),
    189–194. — The framework within which the ILD offset of A.7 finds its formal equivalent.
20. Nelson, P. A., Hamada, H. & Elliott, S. J. (1992). Adaptive Inverse Filters for Stereophonic
    Sound Reproduction. *IEEE Trans. Signal Processing* 40(7), 1621–1632.

**Experimental validation of crosstalk cancellation**

21. Damaske, P. (1971). Head-Related Two-Channel Stereophony with Loudspeaker Reproduction.
    *J. Acoust. Soc. Am.* 50(4B), 1109–1115. — Demonstration of the binaural capability of
    crosstalk cancellation with loudspeakers in an anechoic chamber.
22. Bock, T. M. & Keele, D. B. Jr. (1986). *The Effects of Interaural Crosstalk on Stereo
    Reproduction and Minimizing Interaural Crosstalk in Nearfield Monitoring by the Use of a
    Physical Barrier*, parts 1 and 2. AES 81st Convention, preprint 2420. — The measured version of
    the physical-barrier experiment described in the introduction to the main text. Also cited by
    Glasgal for the same purpose.
23. Takeuchi, T., Kirkeby, O. & Nelson, P. A. (2007). The binaural performance of a cross-talk
    cancellation system with matched or mismatched setup and playback acoustics. *J. Acoust. Soc.
    Am.* 121(2), 1056. — Measured binaural behaviour of digital XTC filters, including the case of
    geometric mismatch.

**Synthetic ambience and ambiophonic architecture**

24. Glasgal, R. (2001). *The Ambiophone — Derivation of a Recording Methodology Optimized for
    Ambiophonic Reproduction*. AES 19th International Conference, Schloss Elmau. — The complete
    ambiophonic chain, and the generation of ambience by convolution with the impulse responses of
    real halls (*Ambiovolver*). It is the point of contrast of A.10.

**Coloration of uncancelled stereo**

25. Vickers, E. *Fixing the Phantom Center: Diffusing Acoustical Crosstalk*. AES. — Quantifies the
    incorrect ILD and ITD that crosstalk introduces for a centered image, the comb filtering of the
    direct field, and the spectral dip around 2 kHz.

**Sum/difference processing**

26. Blumlein, A. D. (1931). British Patent 394,325. — Origin of sum/difference processing.

> **Note.** The bibliographic details of references 17 to 26 come in part from secondary sources
> and should be verified against the AES E-Library, IEEE Xplore or JASA before being considered
> definitive.
