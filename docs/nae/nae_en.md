# NatAmbio Ambient Extractor (NAE): An algorithm for extracting the ambient traces of a stereo recording

**Author:** Raúl Fernández Ortega  
**Date:** June 2026

> **Abstract —** *NatAmbio is a PanAmbio-type spatial reproduction system, based on two stereo dipoles —one frontal and one ambient—, whose operation requires four channels: the main stereo sound and two additional ambience-sound tracks. Since practically all music has been recorded in two-channel stereo format, NatAmbio incorporates its own DSP processing to extract those components from the recording itself. This article presents NatAmbio Ambient Extractor (NAE), an ambience-extraction algorithm developed empirically —through critical listening, analysis of stereo correlations and the mid/side (M/S) representation— and intended to operate in real time at very low computational cost. The spatial component is operationally defined as the one of lower relative level and with its channels strongly anti-correlated, as opposed to a principal component with correlated channels. The algorithm transforms the L/R signal into the M/S domain and applies PCA to a 2×2 covariance matrix, obtaining two mutually decorrelated stereo pairs: a principal one, with correlation +1, and an ambient one, with correlation −1, without resorting to artificial decorrelation. For low-correlation recordings (heavily panned), where the orientation of the PCA axes becomes unstable, a second approach is introduced that adapts the weight of the side component through a parameter β governed by the L/R correlation itself. The implementation details in NatAmbio are described —a sliding PCA window over JACK, temporal smoothing and the α and β modes— as well as its integration with crosstalk cancellation (XTC) in single- and dual-dipole configurations. The result is reproduction compatible with the original musical scene, with greater frontal openness and an enveloping ambience field of adjustable level, obtained from conventional stereo recordings and through real-time processing.*

## Introduction

To have a PanAmbio-type system —two stereo dipoles, one placed at the front and another for surround sound— multichannel recordings are needed that offer not only the main stereo sound but also two additional tracks, which would correspond to the ambience (or surround) sound. This type of recording is singularly scarce, perhaps just a few demonstration examples. It is true that a reconstruction can be made from commercial multichannel recordings of the 5.1 type and similar. But the vast majority of recorded music has been, and continues to be, in stereo format.

NatAmbio, as a system, was born with the intention of being not only an implementation of PanAmbio but also its own DSP software, with the capability to extract the four necessary tracks from the ambient content that may be included in the stereo recording.

There is abundant technical literature devoted to extracting spatial information from stereo recordings, including ambience-extraction and source-separation techniques. This article will present a particular ambient-extraction algorithm that has been developed to fit optimally with the NatAmbio concept, whose objectives are:

- Extract ambient sound from stereo musical recordings, without venturing into the development of more enveloping sound environments of the home-cinema type.
- Be able to perform this extraction in real time, with an algorithm that is simple in processing cost but that achieves the objective of the previous point without significant limitations.

NatAmbio Ambient Extraction (NAE) is an algorithm developed on and for a domestic environment, using as test signals standard, readily available commercial recordings. NAE does not arise from a theoretical search for new spatial transformations, but from the specific use of very well-known DSP techniques and the subsequent analysis of the results when applied to the aforementioned reference recordings commonly used in amateur settings. The algorithm was developed through an iterative process of critical listening, visual analysis of stereo correlations, mid/side (M/S) representation and temporal evolution of PCA components. Several design decisions arose initially from repeated empirical observations and from the study of recurring patterns, and were later formalized in statistical terms. Being an empirical work, it does not originate in a formal mathematical development, so the explanation of the algorithm given here also has this same empirical nature.

## Definition of ambient signal

First it is necessary to define what an ambient signal is, or the ambient component of a stereo signal, in perceptual and statistical terms. This concept lends itself to multiple definitions, depending on the context in which it is applied. The definition presented in this document is oriented to its application in NatAmbio, with no further pretension of forming part of a more general model.

The phenomenon of localization in stereo is controlled by two mechanisms:

1. The level difference between channels. This is what is known as stereo panning. Two channels carrying an identical signal, differentiated only by their relative level, are perceived, when heard through a stereo system, as a single signal oriented toward the side with the higher level. So that, when both levels are equal, this signal is perceived as focused at the center between the loudspeakers.

2. The correlation between the two channels. This mechanism complements the previous one in a more complex way: the correlation between channels does not by itself generate a continuous stereo position, but determines the degree of coherence between the two channels. With correlation close to 1, both channels contain essentially the same signal and localization is governed by the level difference. As the correlation decreases, the channels cease to behave as a single phantom source and progressively come to be perceived as differentiated signals reproduced by each loudspeaker. In the limiting case of zero correlation, the perceptual result corresponds to two independent mono signals, one associated with each loudspeaker. Only when the correlation takes negative values does a component appear that is no longer interpreted as a sum of localizable mono sources, but as a delocalized or surround spatial contribution.

As the negative correlation rises, the perception of delocalization of virtual sources increases. At the extreme, if the level difference between the two channels is also reduced in the process, then the condition of maximum delocalization is reached:

$$ l^2/r^2 = 1$$
$$ corr(l,r) = -1 $$

In this sense, from NAE, the ambient component of a stereo recording can be defined as that with a strong mutual anti-correlation between channels, provided that its relative level over the total recording is significantly low.

Therefore, for NAE, the criterion for extracting the ambience signal will be the component of lower level than the other —which will be the principal one— with its channels anti-correlated with each other. This definition does not aim to describe all the possible spatial phenomena present in a recording, but to establish an operational criterion that makes it possible to design an extraction algorithm.

Finally, there remains the principal component. The original signal can be reconstructed by summing an ambient component and a principal component. Both components are defined as opposite poles of a perceptual-statistical representation of the stereo signal:
 
$$ corr(l_{main}, r_{main}) \approx 1$$
$$ corr(l_{amb}, r_{amb}) \approx -1$$

NAE will try to move the maximum correlation to one pole, the principal, and the maximum anti-correlation toward the ambience pole. In this sense:

1. The localization of the principal component will tend to operate through the basic stereo mechanism of level differences. As already mentioned, the ambient component tends to be delocalized.

2. The extraction of the ambient component can be interpreted as a search for the maximum anti-correlation compatible with the exact reconstruction of the original signal. The principal component then appears as the necessary complement to maintain that reconstruction.

$$ S = C_{main} + C_{amb} $$

The intention of these definitions is to give meaning to the core of NAE's development, without claiming to establish any more general model or theory about stereo. It is evident that any recording presents ambience content that does not correspond to the definition established here. A limiting case is that of any monophonic recording that includes spatial content. For this reason it is necessary to clarify again that, in the context of NatAmbio, the ambience signal has been defined as that which is useful to be recreated and perceived as ambient (delocalized) in a PanAmbio system.
 
## Properties of mid/side (M/S) vs. left/right (L/R)

During the early phases of this algorithm's development, different ways of representing the stereo information were explored. The objective was not only to find a valid mathematical transformation, but to reorganize the information contained in the stereo signal so that certain spatial structures became more evident both in visual analysis and in critical listening to commercial recordings used as test signals.

In this sense, the choice of the M/S representation did not initially arise from a formal mathematical deduction, but naturally from the definition of ambience signal developed in the previous section. The L/R representation is natural for stereo reproduction, but is not especially descriptive from the spatial point of view. By contrast, the M/S representation provides a simple separation between the information common to both channels and the differential information between them. If one thinks in terms of common information and differential information versus correlation and anti-correlation:

$$ mid = l + r; \qquad mid \rightarrow principal $$
$$ side = l -r;  \qquad side \rightarrow ambient$$

From a perceptual point of view, the mid component is associated with what both channels share, while the side component represents the lateral differences present in the recording. As a first approximation, M/S comes closer to the sought decomposition into $C_{main}$ and $C_{amb}$ than the canonical stereo form L/R.

During the study that gave rise to NAE, when representing the samples in the M/S plane it was observed that many musical recordings presented clearly defined geometric structures. This suggested the possibility of applying PCA over that space to automatically identify the dominant directions of energy present in the recording. From a geometric point of view, PCA makes it possible to identify the principal axes of the stereo energy distribution within the M/S plane. As developed later, this interpretation proved especially useful for studying recordings with different spatial characteristics and analyzing the relationship between central content, lateral content and ambient sound. In this context, PCA is not used as a generic dimension-reduction technique, but as a tool to automatically identify the dominant directions associated with the principal/ambient representation defined earlier.

For this reason, although PCA could be applied directly to L/R, the M/S representation produces a much more interpretable geometry for the purpose of ambient extraction, since it allows the relationship between central information (mid) and lateral information (side) to be visualized directly.

## Principal Component Analysis (PCA) within NAE

The next step after the L/R $\rightarrow$ M/S transformation is the application of PCA to these M/S components, taking samples of size N within the recording being studied. Since it is only necessary to diagonalize a 2×2 covariance matrix, the computational cost is extremely low and compatible with real-time processing. The result will yield a principal component and a secondary component, and an eigenvector that is the one allowing the rotation of all N M/S points of the processed sample to axes on which the resulting points will have zero correlation with each other.

The plot presented below makes it possible to understand how PCA acts on each point of a given sample in the M/S space, and how, from a point $(m,s)$, two points $(mC_1, sC_1)$ and $(mC_2, sC_2)$ will be generated with very special properties between them that will be developed later.

<p align="center">
  <img src="images/pca_stereo_01_detalle_v01.png" alt="Projection onto M/S axes">
</p>
<div id="figure_01" align="center"> <strong>Figure 1.</strong> Spatial visualization of the PCA decomposition in the M/S plane</div><br>

Because it is the result of linear transformations —basically rotations— it is feasible to recover the $l$ and $r$ signals again from $(mC_1, sC_1)$ and $(mC_2, sC_2)$, as this is a property inherent to PCA.

Since the algorithm is being applied to musical recordings, the study of results follows a process of empirical validation. As an example of a well-known commercial recording, an excerpt of So What, from the album Kind Of Blue by Miles Davis, has been taken. This recording is very popular and there is abundant technical information available about the recording process, so it is easily recognizable that it presents a significant spatial component, [due to the special nature of the studio where it was recorded and the microphone techniques used](https://es.scribd.com/document/694914875/Kind-of-Blue-Miles-Davis-and-the-Making-of-a-Masterpiece-Ashley-Kahn-Jimmy-Cobb-Z-Library).

## Relationship between stereo correlation and stability of the PCA decomposition

The following plots show, at five arbitrary points of the selected excerpt So What from Kind Of Blue: on the left, the M/S signal itself, and on the right, its representation on the M/S axes, as well as the rotation axes marked by the eigenvectors of the PCA transformation of the sample corresponding to the represented point cloud.

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_midside_000.png" alt="Kind of Blue sample 1">
</p>
<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_midside_001.png" alt="Kind of Blue sample 2">
</p>
<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_midside_002.png" alt="Kind of Blue sample 3">
</p>
<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_midside_003.png" alt="Kind of Blue sample 4">
</p>
<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_midside_004.png" alt="Kind of Blue sample 5">
</p>
<div align="center"> <strong>Figures 2 to 6.</strong> Temporal and spatial visualization of the M/S points for selected samples of So What. The axes of the PCA transformation over the M/S plane are included</div><br>

As can be seen, there is an internal structure in the mid vs side relationship, and the PCA axes mark a rotation in which the first component tends to project more mid signal than side, and vice versa for the second component. That, in the case of this recording, the mid signal is generally greater than its corresponding side signal indicates that the mid signal is really the principal one, where the musical instruments will be more localized.

The evolution of the correlation between the L/R signal is also indicative of the weight of the PCA principal component in the recording:

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_correlation.png" alt="Kind of Blue correlation">
</p>
<div align="center"> <strong>Figure 7.</strong> Plot of the temporal evolution of the l/r correlation for the analyzed sample of So What</div><br>

In the correlation histogram it can be observed that the correlation is very high in most of the samples:

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_correlation_histogram.png" alt="Kind of Blue correlation histogram">
</p>
<div align="center"> <strong>Figure 8.</strong> Histogram of the l/r correlation for the analyzed sample of So What</div><br>

Likewise, the rotation of the PCA axes over the M/S axes is very controlled, rarely going beyond ±20°.

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_eigenvectors_rotation.png" alt="Kind of Blue eingenvectors rotation">
</p>
<div align="center"> <strong>Figure 9.</strong> Temporal evolution of the PCA-axis rotations over the M/S plane</div><br>

There is a direct relationship between all these graphical representations on which it is worth pausing. The figure of temporal evolution of the L/R correlation shows that the analyzed recording presents a predominantly high correlation during most of its duration. Although occasional drops associated with certain musical events appear, the statistical distribution of the correlation is clearly concentrated at values close to 1.

This behavior can be observed more clearly in the correlation histogram. Most of the analyzed windows present high positive correlations, indicating that both channels contain a significant amount of common information. From a perceptual point of view, this usually corresponds to recordings in which the sound scene maintains a well-defined stereo center and where the lateral information represents a relatively small fraction of the total energy.

When the signal is represented in the M/S plane, this high correlation translates geometrically into a clearly elongated point cloud. The signal energy is distributed mainly along a dominant direction, while the dispersion perpendicular to that direction is significantly smaller.

Under these conditions, the application of PCA produces an especially stable decomposition. The first eigenvector is well defined by the geometry of the point cloud and concentrates most of the observed variance. The second eigenvector, orthogonal to the first, captures only the secondary variations present in the recording.

This situation can be appreciated in the plot of temporal rotation of the eigenvectors. Although there are local fluctuations, the principal orientation remains relatively stable over time, reflecting the statistical consistency of the analyzed recording.

From NAE's point of view, this stability is especially relevant. When there is a clearly identifiable dominant direction, the principal component $C_1$ concentrates most of the correlated energy of the recording, while the secondary component $C_2$ collects spatial variations of smaller magnitude.

It is important to note that PCA does not explicitly identify sound sources nor distinguish between direct sound and ambience. However, when the stereo correlation remains high for prolonged intervals, the geometry of the M/S space favors the appearance of a clearly dominant principal component and an energetically reduced secondary component. This circumstance is one of the reasons why NAE's first approach produces especially satisfactory results in recordings such as So What by Miles Davis.

The observation of this relationship between stereo correlation, M/S geometry and stability of the PCA decomposition was one of the elements that motivated the algorithm's subsequent development. Likewise, the analysis of recordings with persistently lower correlations made it possible to identify the limitations of this first approach and led to the development of the second approach described in the following sections.

## Recovery of the L/R presentation

Once $(m,s)$ has been transformed into $(C_1, C_2)$ —where $C_1$ is the principal component and $C_2$ is the secondary one— and both points have been projected onto the reference mid and side axes, we find 4 points: $mC_1$ and $sC_1$ (mid and side of $C_1$) and $mC_2$ and $sC_2$ (mid and side of $C_2$).

From $mC_1$, $sC_1$, $mC_2$ and $sC_2$, the last necessary step is to return to the L/R representation, only that now four components are available, two for left —principal and secondary— and two for right —also principal and secondary.

For the principal component, $C_1$, the left and right channels are obtained by transposition from M/S:

$$ l_{c1} =  \frac {mC_1 + sC_1}{2} $$
$$ r_{c1} =  \frac {mC_1 - sC_1}{2} $$

Likewise for the ambient component, only that in this case the M/S channels proper to $C_2$ are taken:

$$ l_{c2} =  \frac {mC_2 + sC_2}{2} $$
$$ r_{c2} =  \frac {mC_2 - sC_2}{2} $$

The stereo information has been duplicated, moving to a double stereo completely decorrelated from each other. Below, given their special interest, the properties of these two stereophonic pairs are developed.

## Properties of the resulting signals: obtaining the ambient signal

Indeed, $l_{c1}$ and $r_{c1}$ have correlation 1 with each other, $l_{c2}$ and $r_{c2}$ have correlation -1 and, paired up, both $l_{c1}$ and $l_{c2}$, and $r_{c1}$ and $r_{c2}$, have zero correlation. Although this last characteristic is precisely the result of the PCA decomposition itself, the previous two deserve some development.

There are a number of characteristics of this decomposition that are very relevant for the sought objective of being able to extract the spatial sound from a stereo recording. To verify them, we start from the proportionality between $mC_x$ and $sC_x$, for any of the PCA components (x = 1,2).

First, $mC_x$ and $sC_x$ are projections of the component C_x onto the PCA axes (as can be seen in an earlier plot showing an example of these projections)

$$ mC_x = v_{x1} C_x$$
$$ sC_x = v_{x2} C_x$$

Then $mC_x$ and $sC_x$ are proportional:

$$ sC_x =  \frac {v_{x2}} {v_{x1}} mC_x  = k_x * mC_x $$

Therefore, recomposing the L/R signals from M/S, we find that $l_{cx}$ and $r_{cx}$ are also proportional:

$$ l_{cx} = \frac {mC_{x} + sC_{x}}{2}  = \frac {1 + k_x}{2} mC_x $$
$$ r_{cx} = \frac {mC_{x} - sC_{x}}{2}  = \frac {1 - k_x}{2} mC_x $$

Since the eigenvector of component $C_1$ is generally closer, in rotation, to the mid component, we can assume that:

$$ |v_{11}| > |v_{12}| \rightarrow |k_1| < 1 $$

So for the principal component:

$$
l_{c1} = \lambda \times r_{c1} \quad \text{always with } \lambda > 0
$$

Therefore the correlation between $l_{c1}$ and $r_{c1}$ will always be 1.

By contrast, for $C_2$ the sign is just the opposite, being closer to the side component:

$$ |v_{22}| > |v_{21}| \rightarrow |k_2| > 1 $$

Then:

$$
l_{c2} = \lambda \times r_{c2} \quad \text{always with } \lambda < 0
$$

Therefore the correlation between $l_{c2}$ and $r_{c2}$ will always be exactly -1.

The extraction of a stereo component with correlation 1 and another exactly with correlation -1 is not the result of an artificial decorrelation processing, but a direct consequence of representing both PCA components in the L/R coordinate system.

These properties of the algorithm, which are a natural consequence of the M/S decomposition, are especially interesting from the perceptual point of view. Having obtained, by PCA decomposition, an anti-correlated secondary component places it as a candidate for the ambient component defined at the beginning of this development. Likewise, obtaining a principal signal with unit correlation places it as a candidate for the principal component, according to its prior definition.

Therefore, by a mathematically simple procedure, two pairs of signals with characteristics compatible with those sought have been obtained. After numerous empirical tests, it has been verified that, with high-correlation recordings, the secondary component usually matches the ambience very well. And the principal component incorporates the main (musical) information in the foreground, with a simple stereo balance (level panning without any phase contribution). And all of this extracted from the original information itself and through linear transformation processes without prior arbitrary parameters.

This model works very well on recordings such as So What from Kind of Blue, with the main characteristic that the left and right signals have a high correlation, which implies that the mid signal dominates over the side and the pair of signals obtained through PCA will have a very marked relationship of dominance of one over the other.

As can be verified in the analyzed case, the $C_1$ component presents a significantly higher level than $C_2$ throughout the analyzed interval. So the second condition necessary to assume that $C_2$ is an ambient signal, as defined by the NatAmbio paradigm, is met.

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_eigenvalues.png" alt="Eigenvalues evolution">
  </p>
<div align="center"> <strong>Figure 10.</strong> Temporal evolution of the levels of each component C<sub>1</sub> and C<sub>2</sub> and the level differences between them</div><br>

Regarding the relative L/R panning of each of the two components, there is a special characteristic that is worth highlighting. Since for the $C_1$ component its L/R representation is:

$$ l_{c1} = \frac {mC_{1} + sC_{1}}{2}  = \frac {1 + k_1}{2} mC_1 $$
$$ r_{c1} = \frac {mC_{1} - sC_{1}}{2}  = \frac {1 - k_1}{2} mC_1 $$

The L/R balance of $C_1$ and $C_2$ depend on:
$$ \frac {l_{c1}}{r_{c1}} = \frac {{1 + k_1}}{1 - k_1}$$

$$ \frac {l_{c2}}{r_{c2}} = \frac {{1 + k_2}}{1 - k_2}$$
Since the components $C_1$ and $C_2$ are orthogonal to each other (see [figure 1](#figure_01)), it holds that:

$$ {k_1} \cdot {k_2} = -1 \rightarrow k_2 = \frac {-1}{k_1}$$

So that:

$$ \frac {l_{c2}}{r_{c2}} = \frac {{1 + k_2}}{1 - k_2} = \frac {{1 + \frac {-1}{k_1}}}{1 - \frac {-1}{k_1}} = \frac {k_1 - 1}{k_1 + 1} = - \frac {r_{c1}}{l_{c1}}$$

Computing the relative L/R levels, it results:

$$ \left| \frac {l_{c2}}{r_{c2}} \right| = \left| \frac {r_{c1}}{l_{c1}} \right|$$

Which means that the components $C_1$ and $C_2$ present inverse L/R balances: when one component is panned to one side, the other is panned in the opposite direction. This can be appreciated visually in the following plot:

<p align="center">
  <img src="alpha_processed/davis_kind_of_blue_so_what_lr_differences.png" alt="C1_C2 L_r panning evolution">
  </p>
<div align="center"> <strong>Figure 11.</strong> Temporal evolution of the L−R level difference (balance/panning) of each component C<sub>1</sub> and C<sub>2</sub> for the analyzed sample of So What</div><br>

They are curves with a strong symmetry: when $C_1$ is panned in one direction, $C_2$ is panned in the opposite one. The combination produces the graphical sensation that the surround sound generated (always according to the NatAmbio paradigm) by a musical source strongly panned to one side is strongly panned to the opposite side, and the greater the principal panning, the greater the opposite ambient panning. Intuitively, it seems that the localization mechanisms of a recording with high inter-channel correlation include the well-known level panning, but also include a signal decorrelated from the principal one that offers an opposite panning. It is noted here that this resulting property of the NAE algorithm is a possible candidate for a specific study relating panning, decorrelation/anti-correlation and reconstruction of spatial perception from a stereo recording.

## Limitations of the first approach: low-correlation recordings

Up to now, the model presents coherent results because it has been applied to stereo signals with very high correlation. This is not always the case; numerous recordings can be found where the L/R correlation is very low. This happens when the sound sources are very strongly panned to one side or the other, something that has occurred and continues to occur in numerous recordings. If until now we have associated ambient component with strongly panned sources, for these recordings the association ceases to work directly.

As already mentioned, this algorithm was developed with an empirical analysis methodology using commercial recordings themselves as test signals. For the analysis of this new case, the development has been carried out on a recording with very high instrumental panning, the track I Am In Love from the album [At The Blackhawk 3 by the group Shelly Manne and his Men](https://en.wikipedia.org/wiki/At_the_Black_Hawk_3). This album was recorded live in a jazz club, so one is sure that natural atmospheric sound exists, but, as is usual in recordings by the Contemporary Records company, the instruments are very heavily panned and there is no significant central presence. It is a clear case of a low-correlation recording, which is analyzed below.

Starting with the temporal evolution of the correlation over a sample of the aforementioned recording, it can be seen that this is a very different case from Kind Of Blue:

<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_correlation.png" alt="At the Blackhawk correlation">
  </p>
<div align="center"> <strong>Figure 12.</strong> Plot of the temporal evolution of the L/R correlation for the analyzed sample of I Am In Love</div><br>

In the correlation histogram it can be observed that the correlation is much lower, with a peak around a value as low as 0.25:

<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_correlation_histogram.png" alt="At the Blackhawk correlation histogram">
  </p>
<div align="center"> <strong>Figure 13.</strong> Histogram of the L/R correlation for the analyzed sample of I Am In Love</div><br>

Moreover, the histogram is much wider, and many points are in negative correlation, which indicates numerous transitions to statistically different regions, and in turn anticipates the difficulties that PCA will face in indicating the principal direction in the M/S plane.

The strong relationship between the L/R correlation and the rotation of the PCA eigenvectors over M/S appears again, although with a different result: in this case, low L/R correlation implies more aggressive rotations of the PCA axes. One estimator makes it possible to assess the other, which will have relevant consequences in the following sections.

<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_eigenvectors_rotation.png" alt="At the Blackhawk eingenvectors rotation">
  </p>
<div align="center"> <strong>Figure 14.</strong> Temporal evolution of the PCA-axis rotations over the M/S plane for I Am In Love</div><br>

These first plots indicate that, when the stereo correlation decreases in a sustained manner, the orientation of the eigenvectors ceases to reflect a global spatial structure of the recording and comes to be determined by local musical events. This fits the reality of the recording with high panning of the virtual sources.

Below, the M/S information and the PCA axes of five arbitrary moments of the process are presented graphically:
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_midside_000.png" alt="At the Blackhawk sample 1">
</p>
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_midside_001.png" alt="At the Blackhawk sample 2">
</p>
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_midside_002.png" alt="At the Blackhawk sample 3">
</p>
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_midside_003.png" alt="At the Blackhawk sample 4">
</p>
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_midside_004.png" alt="At the Blackhawk sample 5">
</p>
<div align="center"> <strong>Figures 15 to 19.</strong> Temporal and spatial visualization of the M/S points for selected samples of I Am In Love. The axes of the PCA transformation over the M/S plane are included</div><br>

It can be appreciated that the M/S information is very dispersed, with very high rotation of the principal component over the mid axis, which will make it difficult for the first approach to the NAE model to offer an ambience signal. In the case of the third and fourth plots, it would even seem that the principal axis is not such. In those cases, the variance associated with both components is very similar. This can be verified from the comparative level plot between $C_1$ and $C_2$, where it can be seen that the difference is much smaller than in the case of So What. There are many points where $level(C_1) \approx level(C_2)$.

<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_eigenvalues.png" alt="Eigenvalues evolution">
  </p>
<div align="center"> <strong>Figure 20.</strong> Temporal evolution of the levels of each component C<sub>1</sub> and C<sub>2</sub> and the level differences between them for the I Am In Love sample</div><br>

The result is that, while for the case of Kind Of Blue the direct listening of the obtained ambience signal offers a subjectively plausible sensation, for the recording of I Am in Love the subjective sensation is not one of spatial perception. In this last case, one can perceive how the instruments enter and exit the component called "ambience". What actually results is that the algorithm is oriented more toward being a source separation than an ambient extractor. At a given moment of the recording, a trumpet solo in the left channel "competes" with the drums in the right one. The result is that, on occasions, the trumpet enters the ambience and on occasions it is the drums that appear, with no criterion other than their respective energies. An oscillating perception, far from the proposed objective.

The comparative analysis of both recordings, Kind of Blue vs At The Blackhawk, shows that the perceptual quality of a first approach to the NAE algorithm depends strongly on the statistical stability of the stereo signal. When there is a clearly defined dominant direction, PCA produces a perceptually satisfactory separation. However, in recordings with moderate or low correlation sustained over time, the decomposition begins to identify legitimate musical elements associated with stable lateral positions as independent components.

NAE's first approach can be interpreted as a search for the principal/ambient poles defined earlier. However, this approach implicitly presupposes that there is a clearly identifiable dominant direction in the M/S space. When the stereo correlation decreases, that hypothesis ceases to hold and PCA begins to identify strongly panned musical elements as independent components. This observation motivated the development of a second approach, which will be presented below. The objective was no longer to modify the geometry of the PCA decomposition, but to dynamically adapt the amount of information transferred to the spatial component as a function of the global statistical state of the recording, estimated from the L/R stereo correlation.

## A second approach to the NAE algorithm

Assuming that the problem may be due to the low correlation between L/R channels, a transformation can be made prior to the PCA step that increases this correlation:

$$ l' = ( 1- \gamma)\space l + \gamma \space r$$
$$ r' = ( 1- \gamma)\space r + \gamma \space l$$

In this way the M/S signals become:

$$ m = l + r $$
$$ s = ( 1 - 2 \gamma) (l -r) = \beta (l-r)$$
$$ \text{with}\space \beta = ( 1 - 2 \gamma), \qquad \gamma \in [0,\, 0.5] \;\Rightarrow\; \beta \in [0,\, 1]$$

In this way, with the factor $\beta$, the side component can be reduced in the M/S weight. The pending question is to decide what the best setting of $\beta$ will be, which will necessarily have to depend on the nature of the recording itself. If in the case of Kind of Blue it is not needed, or a $\beta$ factor close to 1 will suffice, in the case of At The Blackhawk it seems logical to think that it will be convenient to introduce a smaller $\beta$. In any case, it is evident that it must be the recording itself, with its correlation characteristics, that generates its own $\beta$, variable with its evolution.
The parametrization proposed for application in the NAE algorithm to generate $\beta$ starts from the previously discussed observation that there is a strong relationship between the L/R correlation and the rotation of the PCA axes. If the objective is to avoid this rotation, a candidate for the parametrization of $\beta$ is the L/R correlation itself. Correlation is not used only as a perceptual measure of stereo width. In the context of NAE it also acts as an indirect indicator of the stability of the PCA decomposition. High correlations produce more elongated M/S clouds and more stable eigenvectors, while low correlations generate more isotropic distributions and PCA orientations more sensitive to small statistical variations.

Empirically, the proposal included in NAE is:

$$ \rho_{lr} = corr(l, r)$$

$$ \beta = 0.55 + 0.45\space|\rho_{lr}| $$

Correlations close to 1 in absolute value will give rise to $\beta \approx 1$, so that the side component will hardly change its weight, while for correlations close to zero, $\beta$ $\approx$ 0.55, which has been empirically established as the minimum weight of the side component in this new transformation. The minimum value 0.55 does not arise from a formal mathematical optimization but from the empirical evaluation of multiple reference recordings. Lower values produce an excessive collapse of the stereo image, while higher values do not sufficiently stabilize the PCA orientation in strongly localized to the sides recordings. In any case, its nature is arbitrary and, functionally in NAE, any kind of increasing relationship between $corr(l/r)$ and $\beta$ is acceptable. Its perceptual result and subjective evaluation will be quite different.

Below are the plots of the evolution of the $l'/r'$ correlation, its histogram and the PCA eigenvector rotations for the application of this transformation on the same sample of I Am In Love:
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_correlation.png" alt="At the Blackhawk correlation">
  </p>
<div align="center"> <strong>Figure 21.</strong> Plot of the temporal evolution of the L/R correlation for the analyzed sample of I Am In Love after applying the beta modeling</div><br>

The temporal evolution of the $l'/r'$ correlation comes closer to unity. In the correlation histogram it can be observed that the correlation rises to values between 0.60 and 0.75.
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_correlation_histogram.png" alt="At the Blackhawk correlation histogram">
  </p>
<div align="center"> <strong>Figure 22.</strong> Histogram of the L/R correlation for the analyzed sample of I Am In Love after applying the beta modeling</div><br>

Moreover, the histogram narrows, and far fewer points are now in negative correlation, which indicates that it will be easier for PCA to mark a new principal direction, in a more stable way.

The desired effect has been achieved: the rotations of the PCA axes are now bounded mainly within a reasonable margin of $20^{\circ}$.

<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_eigenvectors_rotation.png" alt="At the Blackhawk eingenvectors rotation">
  </p>
<div align="center"> <strong>Figure 23.</strong> Temporal evolution of the PCA-axis rotations over the M/S plane for I Am In Love after applying the beta modeling</div><br>

Below, the new representations of M/S points and PCA axes are shown for the same 5 samples of the situation prior to this newly added transformation:
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_midside_000.png" alt="At the Blackhawk sample 1">
</p>
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_midside_001.png" alt="At the Blackhawk sample 2">
</p>
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_midside_002.png" alt="At the Blackhawk sample 3">
</p>
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_midside_003.png" alt="At the Blackhawk sample 4">
</p>
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_midside_004.png" alt="At the Blackhawk sample 5">
</p>
<div align="center"> <strong>Figures 24 to 28.</strong> Temporal and spatial visualization of the M/S points for selected samples of I Am In Love. The axes of the PCA transformation over the M/S plane after applying the beta modeling are included</div><br>

Without being ellipse shapes as clean as when Kind Of Blue was analyzed, now the principal component does mark a rotation closer to the mid axis, carrying considerably more energy than its opposing second component.

The empirical evaluation of listening to the signal called ambient in a stereo system maintains, at a much lower level, a certain presence of the instruments, with much more stability, without such abrupt changes from the appearance and disappearance of those very lateral sources. It approaches in a much more reasonable way an ambience signal that meets the definition the NAE system needs, one of the main objectives of this development. This is confirmed by the analysis of the new relative levels between $C_1$ and $C_2$:
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_eigenvalues.png" alt="Eigenvalues evolution">
  </p>
<div align="center"> <strong>Figure 29.</strong> Temporal evolution of the levels of each component C<sub>1</sub> and C<sub>2</sub> and the level differences between them for the I Am In Love sample after applying the beta modeling</div><br>

It must be borne in mind that this second approach to NAE does not aim to artificially increase the amount of spatial sound present in the recording. Its objective is to reduce PCA's tendency to interpret dominant lateral sources as independent components liable to be transferred to the ambient output.

NatAmbio uses both NAE approaches in its two stereo dipoles, with different objectives but in a coordinated manner between the two. The following sections describe the transition from the description of its algorithm to the final implementation in real-time processing software that forms part of the NatAmbio application.

Finally, also with NAE in this new modeling, the expected effect of opposite panning between the principal component and the ambience signal is also produced.
<p align="center">
  <img src="beta_processed/manne_his_men_i_am_in_love_lr_differences.png" alt="C1_C2 L_r panning evolution">
  </p>
<div align="center"> <strong>Figure 30.</strong> Temporal evolution of the L−R level difference (balance/panning) of each component C<sub>1</sub> and C<sub>2</sub> for I Am In Love after applying the beta modeling</div><br>

For comparative purposes, in the plot of relative levels between $C_1$ and $C_2$ after the NAE application without the beta model, the very high panning that this recording includes can be appreciated.
<p align="center">
  <img src="alpha_processed/manne_his_men_i_am_in_love_lr_differences.png" alt="C1_C2 L_r panning evolution">
  </p>
<div align="center"> <strong>Figure 31.</strong> Temporal evolution of the L−R level difference (balance/panning) of each component C<sub>1</sub> and C<sub>2</sub> for I Am In Love without applying the beta model</div><br>

## Implementation of NAE

Up to this point, the conceptual foundations of NAE and the observations that led to its formulation have been described. This section presents the practical implementation details used in NatAmbio, among which are the sample sizes for PCA, possible temporal smoothing mechanisms and the procedure for computing the β parameter used in the second approach.

The first decision to adopt concerns the sample size over which the successive PCA steps will be applied. The sample size used by PCA constitutes a compromise between temporal resolution and statistical stability. Small windows allow the spatial variations of the recording to be tracked quickly, but produce noisier estimates of the covariance matrix. Large windows improve the stability of the eigenvectors, although they reduce the ability to adapt to rapid changes in the stereo content.

In the current implementation a PCA window equivalent to five consecutive audio blocks is used. When NAE operates within NatAmbio, it does so in real time over [JACK Audio Connection Kit](https://jackaudio.org/). In this case the size of each of those blocks coincides with the samples-per-cycle size configured in the audio server, with values of 250 or 500 samples recommended as reasonable sizes for a PCA process that adapts to the variability of the variance matrices in a way that is neither too abrupt nor too smooth.
Therefore, it must be taken into account that each audio sample participates in five consecutive PCA analyses. The five contributions obtained for each point are subsequently averaged, producing an effect equivalent to a temporal smoothing of the PCA orientation. The (re)normalization of levels after averaging has followed a simple safety criterion, since in this case the possibility of inverse transformation is lost. That is, the averaging itself over several PCA computations, in sliding-window mode, prevents the mathematical condition for reconstructing the original signal from the obtained components from being met. Some fidelity to the input signal is lost in exchange for obtaining components whose temporal evolution is smoothed.

In any case, within NAE in NatAmbio, there is the possibility of arbitrarily readjusting the gains for each of the obtained components. On the other hand, no perceptible advantage was observed when using additional weighting functions, so the final implementation uses a rectangular sliding window.

The computation of the L/R correlation used to determine β uses a more extensive time window than that used by PCA. Specifically, twenty consecutive windows are considered. This decision introduces a more stable estimate of the global correlation of the recording, avoiding excessively rapid variations of $\beta$ that could be transferred to the spatial processing.

Due to the real-time operation requirements, this estimate mainly uses past information of the signal, accepting a small compromise between temporal symmetry and latency.

As explained, NAE finally has two possible implementations: a first approach, without specific adaptation of the input signal, which, for NatAmbio, is called NAE $\alpha$ mode; and a second approach, with special adaptation of the input signal using the $\beta$ parameter, which in NatAmbio is called NAE $\beta$ mode.

It is worth clarifying the notation to avoid confusion. The mode names $\alpha$ and $\beta$ identify the two implementations of NAE and must not be confused with the parameters used in the equations: the mixing coefficient $\gamma$ and the side-component weight $\beta = 1 - 2\gamma$. The $\alpha$ mode corresponds to the case without prior mixing ($\gamma = 0$, equivalent to $\beta = 1$), while the $\beta$ mode applies the dynamic adaptation of $\beta$ as a function of the L/R correlation.

Functional scheme of NAE, both modes, $\alpha$ and $\beta$:

<p align="center">
  <img src="images/nae_implementation_flow_01.svg" alt="NAE scheme first stage">
  </p>
<div align="center"> <strong>Figure 32.</strong> Scheme of the implementation of the NAE algorithm, in its alpha and beta modes</div><br>

## Application of NAE to single- or dual-dipole NatAmbio

NAE was not conceived as a standalone signal-separation algorithm, but as the first stage of a complete spatial reproduction system called NatAmbio. For this reason, the software implementation of the NAE algorithm is adapted to this specific use.

Within NatAmbio, NAE and [XTC](../xtc/xtc_filters_en.md) perform complementary functions. NAE reorganizes the spatial information present in the recording, separating predominantly frontal components and predominantly ambient components. XTC subsequently acts on the physical reproduction, reducing the acoustic crosstalk between the two ears and improving the spatial precision with which those components are perceived. Specifically, the ambient component extracted by NAE, whether in its frontal or surround implementation, is projected by XTC, increasing the aforementioned sensation of delocalization. Crosstalk cancellation acts as an amplifier of the ambience effect.

For this reason, the $\alpha$ mode of NAE can be applied to a single-dipole NatAmbio, the frontal one. On the signal decomposed into principal and ambient components, the stereo channels sent to this dipole can be recomposed with modified gains: slightly attenuating, by around 1 dB, the principal component and amplifying the ambient component by around 4 dB; the effect that XTC enhances is that the enveloping sensation originally present in the recording is amplified, and the musical scene is also widened in the case of recordings with high instrumental panning. All of this with hardly any modification of the natural panning or the tonal balance of the principal component. Obviously, more aggressive modifications of the relative weight between principal component and ambient component produce very dry sound scenes (without ambience) or overly resonant ones (with very present ambience). In any case, the adjustment of these weights to the taste of the system's end listener is within NatAmbio's possibilities.
<p align="center">
  <img src="images/ambio_one_dipolo.svg" alt="NatAmbio One dipole">
  </p>
<div align="center"> <strong>Figure 33.</strong> Scheme of the application of NAE to a single-dipole NatAmbio system</div><br>

For the case of complete NatAmbio, with two dipoles, the proposal for the frontal dipole is maintained: extraction of components by $\alpha$ mode and rebalancing between them to the listener's taste.

With the surround dipole, whose objective is to widen the enveloping sensation, the application of NAE is the one corresponding to the $\beta$ mode. Given its very lateralized rear location, it is very important that the L/R channels sent do not have strongly panned instrumental signals, because they will be perceived behind the listener, causing an undesired sensation. By contrast, if the L/R channels sent come from the NAE decomposition in $\beta$ mode, and only the anti-correlated ambient component itself is sent —discarding the principal component— the ambience effect is enhanced. That perceptual effect, in this case, has been formed by the composition of two ambient components: that of NAE $\alpha$ mode acting on the frontal dipole and that of NAE $\beta$ mode acting on the surround dipole. The possibility of adjusting the relative gains between the two components allows the listener to adjust the enveloping sensation to their taste.

It must be taken into account that the ambient dipole's own XTC also acts on it, so the ambient component of the $\beta$ mode, sounding through it exclusively, without any principal component, generates a perception that is not rear, but delocalized. The surround dipole can be arranged with its loudspeakers at a closed angle (around $40^{\circ}$) and the XTC effect will do its job of moving the decorrelated signal of NAE $\beta$ mode to the delocalized space.

![NatAmbio Two dipole](images/ambio_two_dipole.svg)
<div align="center"> <strong>Figure 34.</strong> Scheme of the application of NAE to a dual-dipole NatAmbio system</div><br>

In this way, one of the fundamental objectives of NatAmbio is achieved: to generate an additional ambient component extracted from the recording itself, without external artifices, to feed a PanAmbio-type double-dipole system and reproduce it in a way perceptually coherent with the rest of the sound scene.

NAE constitutes the first stage of the NatAmbio processing chain, followed by XTC and finalized by a DRC-type equalization. The three stages —four, if the loudness effect that DRC can incorporate is also considered— present a high synergy and act in a complementary manner.

The result is reproduction compatible with the original musical scene, but with a greater sensation of frontal openness and an enveloping ambience field whose level can be adjusted to the listener's taste. All of this is obtained from conventional commercial stereo recordings and through real-time processing.
