# Applying NatAmbio XTC in non-symmetric setups

**Author:** Raúl Fernández Ortega  
**Date:** July 2026

This technical note extends the design of XTC filters for conventional stereo environments to layouts in which, for whatever reason, the desired symmetry does not hold. The asymmetry may lie in loudspeaker placement, but also — and this is probably the more frequent case — in the acoustic environment of an otherwise symmetric placement: a wall close to one side only, different furniture to left and right. As the validation section shows, what the model requires is not that the geometry be asymmetric, but that the crossed paths be.

## Notation

| Symbol | Meaning |
|---|---|
| $H_{ll},\ H_{rr}$ | Direct acoustic paths (loudspeaker → same-side ear) |
| $H_{lr},\ H_{rl}$ | Crossed acoustic paths (loudspeaker → opposite ear) |
| $G_l = H_{lr}/H_{ll}$ | Normalised crossed transfer function of the left loudspeaker |
| $G_r = H_{rl}/H_{rr}$ | Normalised crossed transfer function of the right loudspeaker |
| $b = H_{rr}/H_{ll}$ | Balance of the asymmetric stereo system |
| $g_x,\ S_x$ | Broadband part and spectral shape of $G_x = g_x \ast S_x$ |
| $\bar{S}$ | Spectral shape of the round trip (mean of the log-magnitudes of $S_l$ and $S_r$) |
| $P = G_l \ast G_r$ | Round-trip operator: one complete rung of the ladder (when implemented, $g_l \ast g_r \ast \bar{S}$) |
| $\mathbf{H}$ | Acoustic transfer matrix of the asymmetric system |
| $\mathbf{M}$ | Normalised relative coupling matrix (asymmetric counterpart of $\mathbf{C}_G$) |
| $\mathbf{D}$ | Diagonal balance matrix, $\mathbf{D} = \mathrm{diag}(1, b)$ |
| $\mathbf{F}_{XTC}$ | XTC filtering matrix (direct and cross filters) |
| $\Theta_l,\ \Theta_r$ | Incidence azimuth of each loudspeaker |
| $\delta$ | Unit impulse (neutral element of convolution) |
| $\ast$ | Convolution operator |
| $N$ | Number of terms (iterations) in the summation |

## Model formulation and mathematical development

We start again from the sound scene of a basic stereo system, showing the acoustic crosstalk paths:

![Stereo sound scene](images/esquema_XTC_01.svg)


As already developed in [Design of a convolution-based stereo crosstalk canceller (XTC) for NatAmbio](xtc_filters_en.md), the general matrix of acoustic paths can be defined as:

```math
\mathbf{H} = \begin{bmatrix} H_{ll} & H_{rl} \\ H_{lr} & H_{rr} \end{bmatrix}
```

If we take the direct acoustic paths out of the problem, since they will be [handled by equalisation through DRC and not by XTC](xtc_filters_en.md#what-is-inverted-and-what-is-not-separation-between-xtc-and-drc), we can make a first decomposition of the matrix $\mathbf{H}$:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & {H_{rl}}/{H_{ll}} \\ {H_{lr}}/{H_{ll}} & {H_{rr}}/{H_{ll}} \end{bmatrix}
```

Assuming that both direct paths will subsequently be equalised by DRC so as to obtain a similar response at the listening position, we can approximate:

```math
{H_{rr}} = b \cdot H_{ll}
```

where $b = H_{rr}/H_{ll}$ is the linear ratio between the levels of the two direct paths of the asymmetric system, which is what is usually called the "balance" of the stereo system.

It is worth pausing on the scope of this approximation. The ratio $H_{rr}/H_{ll}$ is, strictly speaking, a full transfer function: it captures both the difference in frequency response between the two direct paths and the difference in time of flight arising from the different loudspeaker-to-listener distances typical of an asymmetric layout. That transfer function, however, is part of the inversion of $H_{ll}$ and $H_{rr}$ — that is, of the treatment of the direct paths — which, following the separation between XTC and DRC established in the main note, belongs entirely to the DRC stage and not to XTC filtering. Once DRC has matched the spectral shape and the delay of both direct paths, all that is left of that ratio is a real scalar: the relative gain between channels. The XTC model therefore ends up modelling $b$ as a scalar and deliberately accepts whatever residual acoustic mismatch may remain, both because it is not caused by the XTC filtering and because DRC, by its very nature, does not deal with the balance between channels.

Furthermore, as already defined in [Design of a convolution-based stereo crosstalk canceller (XTC) for NatAmbio](xtc_filters_en.md#problem-analysis-and-resolution), we can use the term $G$:

```math
G = \frac{H_{cross}}{H_{direct}}
```

```math
G_{l} = \frac{H_{lr}}{H_{ll}}
```

```math
G_{r} = \frac{H_{rl}}{H_{rr}}
```

The matrix $\mathbf{H}$ then decomposes as:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & b \cdot G_{r} \\ G_{l} & b \end{bmatrix}
```

The matrix to be inverted through the recursive development shows two differences with respect to the symmetric case, both due to the asymmetry of the system itself: the first is that $G_{l} \neq G_{r}$, and the second is that a balance $b$ must be included in one of the two channels in order to equalise the sound pressure at the listening position.

Note that the model does not change. The only difference with respect to the symmetric case is that the system goes from using a single function $G$ to using two distinct functions, $G_{l}$ and $G_{r}$, one per loudspeaker.

The matrix $\mathbf{H}$ can be further decomposed into:

```math
\mathbf{H} = H_{ll} \begin{bmatrix} 1 & G_{r} \\ G_{l} & 1 \end{bmatrix} \cdot \begin{bmatrix} 1 & 0 \\ 0 & b \end{bmatrix} = H_{ll} \cdot \mathbf{M} \cdot \mathbf{D}
```

so that the matrix to be inverted in order to obtain the XTC filters is:

```math
\mathbf{M} = \begin{bmatrix} 1 & G_{r} \\ G_{l} & 1 \end{bmatrix}
```

The inversion of the complete acoustic path $\mathbf{H}$ is:

```math
\mathbf{H}^{-1} = \left( H_{ll} \cdot \mathbf{M} \cdot \mathbf{D} \right)^{-1} = H_{ll}^{-1} \cdot \begin{bmatrix} 1 & 0 \\ 0 & 1/b \end{bmatrix} \cdot \mathbf{M}^{-1}
```

Note the order of the factors: $H_{ll}$ is a scalar and commutes, but $\mathbf{D}$ and $\mathbf{M}$ do not, so $\mathbf{D}^{-1}$ must pre-multiply $\mathbf{M}^{-1}$. As will be seen later, it is precisely this position that makes the gain $1/b$ act on the signal delivered to the right loudspeaker.

Consistently with the separation between XTC and DRC of the main note, NatAmbio does not implement the complete acoustic inverse: the factor $H_{ll}^{-1}$ is delegated to DRC and the XTC filtering matrix is

```math
\mathbf{F}_{XTC} = \mathbf{D}^{-1} \cdot \mathbf{M}^{-1} = \frac{1}{1 - G_{l} G_{r}} \begin{bmatrix} 1 & -G_{r} \\ -G_{l}/b & 1/b \end{bmatrix}
```

so that $\mathbf{H} \cdot \mathbf{F}_{XTC} = H_{ll} \cdot \mathbf{I}$: each ear receives only the signal intended for it, at the same level in both, and through the natural direct paths, which remain intact.

## Application in NatAmbio

As already shown in the main note on XTC for NatAmbio, inverting the matrix $\mathbf{M}$ is equivalent to its recursive solution. It is worth going through the development in detail, because it is there that it is decided which function $G$ ends up in each cross filter.

The inverse of a $2\times2$ matrix in the convolution algebra is obtained just as in the scalar case, since convolution is commutative:

```math
\mathbf{M}^{-1} = \frac{1}{\det \mathbf{M}} \begin{bmatrix} \delta & -G_{r} \\ -G_{l} & \delta \end{bmatrix},
\qquad \det \mathbf{M} = \delta - G_{l} \ast G_{r}
```

The determinant is precisely $\delta - P$, with $P = G_{l} \ast G_{r}$: the round-trip operator is not an auxiliary definition introduced for convenience, but the object that appears of its own accord when inverting $\mathbf{M}$, which is why the convergence condition falls on it and not on each $G$ separately.

As long as $\left| P \right| < 1$, the inverse of the determinant admits a geometric series expansion, which truncated to $N$ terms is:

```math
\left( \delta - P \right)^{-1} = \sum_{i=0}^{\infty} P^{i} \simeq \delta + \sum_{i=1}^{N} P^{i}
```

Substituting, the filtering matrix becomes:

```math
\mathbf{M}^{-1} \simeq \left( \delta + \sum_{i=1}^{N} P^{i} \right) \ast \begin{bmatrix} \delta & -G_{r} \\ -G_{l} & \delta \end{bmatrix}
= \begin{bmatrix} F^{direct} & F^{cross}_{l} \\ F^{cross}_{r} & F^{direct} \end{bmatrix}
```

The two diagonal elements are the same development, with no crossed factor to tell them apart, and give the direct filter. The two off-diagonal elements each carry **their own $G$ as a common factor**, inherited from the corresponding cofactor, and give the two cross filters. Written element by element in terms of the acoustic paths:

```math
F^{direct}_{l} = \delta + \sum_{i=1}^{N} \frac {H_{lr}^{i} \ast H_{rl}^{i}} {H_{ll}^{i} \ast H_{rr}^{i}} = \delta + \sum_{i=1}^{N} G_{l}^{i} \ast G_{r}^{i} = \delta + \sum_{i=1}^{N} \left( G_{l} \ast G_{r} \right)^{i}
```

```math
F^{direct}_{r} = \delta + \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i}} {H_{rr}^{i} \ast H_{ll}^{i}} = \delta + \sum_{i=1}^{N} G_{r}^{i} \ast G_{l}^{i} = \delta + \sum_{i=1}^{N} \left( G_{r} \ast G_{l} \right)^{i}
```

```math
F^{cross}_{l} = - \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i-1}} {H_{rr}^{i} \ast H_{ll}^{i-1}} = - \sum_{i=1}^{N} G_{r}^{i} \ast G_{l}^{i-1} = - G_{r} \ast \sum_{i=1}^{N} \left( G_{l} \ast G_{r} \right)^{i-1}
```

```math
F^{cross}_{r} = - \sum_{i=1}^{N} \frac {H_{lr}^{i} \ast H_{rl}^{i-1}} {H_{ll}^{i} \ast H_{rr}^{i-1}} = - \sum_{i=1}^{N} G_{l}^{i} \ast G_{r}^{i-1} = - G_{l} \ast \sum_{i=1}^{N} \left( G_{r} \ast G_{l} \right)^{i-1}
```

The two direct filters are equal by commutativity of convolution, $\left( G_{l} \ast G_{r} \right)^{i} = \left( G_{r} \ast G_{l} \right)^{i}$, and from here on are written without a subscript. Note that **in the cross filters the exponents of the denominator are crossed with respect to those of the numerator**: in $F^{cross}_{l}$ the path $H_{rl}$ appears raised to $i$ and normalised by $H_{rr}^{i}$ — that is, in the form $G_{r}^{i}$ — while the other pair stays at $i-1$. This is an easy point to get wrong, and the check is the first-order term: at $i=1$ the filter must reduce to $-G_{r} = -H_{rl}/H_{rr}$, and not to $-H_{rl}/H_{ll}$, which would be $-b \ast G_{r}$ and would introduce the balance inside $\mathbf{M}^{-1}$, precisely where the model does not want it.

**Relation to the expressions of the main note.** [Design of a convolution-based stereo crosstalk canceller (XTC) for NatAmbio](xtc_filters_en.md#problem-analysis-and-resolution) writes these same four filters in their complete form, with the balance included implicitly, since there the cancellation is solved directly on the acoustic paths without factoring out $\mathbf{D}$. Its cross filters end up normalised by the direct path of the loudspeaker that **radiates** the anti-signal — $F^{cross}_{r}$ evaluated at $i=1$ is $-H_{lr}/H_{rr}$ there — whereas here the elements of $\mathbf{M}^{-1}$ normalise each $G$ by the direct path of its **own** loudspeaker — $-G_{l} = -H_{lr}/H_{ll}$. The two expressions differ by exactly the factor $b$ that $\mathbf{D}^{-1}$ supplies afterwards, so they describe the same filtering; under the symmetry hypothesis of the main note, $b = \delta$ and they agree term by term. When comparing the two notes it is worth keeping in mind which of the two normalisations is being read, because the subscripts of the denominators are not the same.

The truncation criterion is that no filter should exceed the temporal extent $N(\tau_{l} + \tau_{r})$ of the direct one: since the factor $G$ already contributes half a rung of delay, the series accompanying the cross filter is cut one order earlier, so that its last tap falls at $(N-1)(\tau_{l}+\tau_{r}) + \tau_{x}$ and stays within that same extent. This is also what the Horner recurrence delivers naturally, at no additional cost.

In compact form, with $P$ as the round-trip operator:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^i
```

```math
F^{cross}_l = - {G_{r}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{r}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

```math
F^{cross}_r = - {G_{l}} \ast \sum_{i=1}^{N} P^{i-1} = - {G_{l}} \ast \left( \delta + \sum_{i=1}^{N-1} P^{i} \right)
```

The round-trip operator that has appeared as the determinant admits a direct acoustic reading:

```math
P = G_{l} \ast G_{r}
```

it is exactly the path described by one rung of the recursion: the signal leaves the left loudspeaker towards the right ear — factor $G_{l}$ — and comes back from the right loudspeaker towards the left ear — factor $G_{r}$. It is also the product on which the convergence condition is stated, and the object, symmetric under the exchange of channels, on which both direct filters depend.

### Correspondence between filters, inputs and loudspeakers

Writing $\mathbf{x} = (x_{l}, x_{r})$ for the pair of programme signals and $\mathbf{s} = (s_{l}, s_{r})$ for the pair delivered to the loudspeakers, we have $\mathbf{s} = \mathbf{F}_{XTC} \ast \mathbf{x}$, so that **each row of the filtering matrix corresponds to a loudspeaker** and each column to an input:

```math
s_{l} = F^{direct} \ast x_{l} + F^{cross}_{l} \ast x_{r}
```

```math
s_{r} = \frac{1}{b} \left( F^{direct} \ast x_{r} + F^{cross}_{r} \ast x_{l} \right)
```

| Filter | Feeds loudspeaker | Takes input | Contains | First tap at | Level of first tap |
|---|---|---|---|---|---|
| $F^{cross}_{l}$ | left | right | $G_{r}$ | $\text{ITD}_{r}$ | $-\text{ILD}_{r}$ |
| $F^{cross}_{r}$ | right | left | $G_{l}$ | $\text{ITD}_{l}$ | $-\text{ILD}_{l}$ |

Everything in the left cross branch is "right" — the input, the function $G$, the ITD and the ILD — except the loudspeaker that radiates it. The reason is physical and not a writing convention: the leakage to be cancelled at the left ear is that of the **right** loudspeaker, described by $G_{r}$; but the anti-signal that cancels it has to reach that same left ear, and the only direct path that gets there is that of the **left** loudspeaker. Hence the crossing: the subscript of the filter names the loudspeaker that radiates, the subscript of the $G$ it contains names the loudspeaker whose leakage it cancels, and they are always opposite.

This can be checked on the contribution to the left ear of the signal $x_{r}$, which is the one that should not arrive there. The right loudspeaker contributes $s_{r}$ through its crossed path $H_{rl}$ and the left one contributes $s_{l}$ through its direct path $H_{ll}$:

```math
e_{l} \big|_{x_{r}} = \frac{x_{r}}{\delta - P} \ast \left( \frac{H_{rl}}{b} - G_{r} \ast H_{ll} \right) = 0
```

since $G_{r} \ast H_{ll} = \left( H_{rl}/H_{rr} \right) \ast H_{ll} = H_{rl}/b$. The cancellation is exact, and it is so **only with the factor $1/b$ in place**: it is the same result as in the section on balance adjustment, seen term by term instead of in matrix form.

### Decomposition of the round-trip operator

The expression $P = G_{l} \ast G_{r}$ is what gives the development its meaning, but it is not what goes directly into code.

Recall that, as was done in the formulation of [Design of a convolution-based stereo crosstalk canceller (XTC) for NatAmbio](xtc_filters_en.md#final-design-development), both $G_{l}$ and $G_{r}$ can be expressed as dependent on:

```math
G_{l} = \delta \left ( \text{ITD}_{l}, \text{ILD}_{l} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{l} \right), \text{ILD}_{avg} \left ( \Theta_{l} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{l}, f \right )
```

```math
G_{r} = \delta \left ( \text{ITD}_{r}, \text{ILD}_{r} \right ) = \delta \left ( \text{ITD} \left ( \Theta_{r} \right), \text{ILD}_{avg} \left ( \Theta_{r} \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta_{r}, f \right )
```

The parametrization of each of these functions can follow the same model developed in the main XTC note for NatAmbio, so that in the step towards implementation each function $G$ is separated into its broadband part and its spectral shape:

$$G_x = g_x \ast S_x$$

$$g_x = \delta \left ( \text{ITD} \left ( \Theta_{x} \right ), \text{ILD}_{avg} \left ( \Theta_{x} \right ) \right)$$

$$S_x = \text{ILD}_{spectrum} \left ( \Theta_{x}, f \right )$$

and the convention established in the main note comes into play — [application of the ILD spectrum within the series](xtc_filters_en.md#application-of-the-ild-spectrum-within-the-series): the spectrum is applied only once per rung, whereas the delay and the broadband attenuation do follow the full power law. The round-trip operator is then generated as

```math
P = g_l \ast g_r \ast \bar{S}
```

that is: the delays of both sides add, the attenuations multiply, and where the product $G_{l} \ast G_{r}$ would accumulate $S_l \ast S_r$ the spectrum intervenes only once, through an averaged shape $\bar{S}$. With this, term $i$ of the direct filter is:

```math
(g_l g_r)^i \ast \bar{S}^i
```

and term $i$ of each cross filter:

```math
g_r (g_l g_r)^{i-1} \ast S_r \ast \bar{S}^{i-1}
```

so that the number of applications of the spectrum is $i$ in every case, with the same index $i$ numbering the same rung in the direct filter and in the cross ones.

It remains to determine which spectral shape corresponds to $\bar{S}$. Since the round trip crosses one head shadow on each side, and there is room for only one application, the natural choice is the mean of both log-magnitudes. Given that the empirical model of $ILD_{spectrum}$:

```math
ILD_{spectrum}(f) = \alpha \cdot 10 \cdot log_{10}(f/1000 + 1) \cdot \sin(\Theta)
```

is linear in the product $\alpha \sin\Theta$, that mean is obtained without averaging any responses: it suffices to evaluate the same model with

```math
\bar{\kappa} = \tfrac{1}{2} \left( \alpha_l \sin\Theta_l + \alpha_r \sin\Theta_r \right)
```

With both sides equal, $\bar{S}$ collapses into $S$ and all the expressions above reduce exactly to those of the symmetric case. The spectrum **of each side individually**, $S_l$ or $S_r$, does however appear in the first-order term of its own cross filter, which is the dominant one and the one that actually cancels the crosstalk: this is what keeps the two cross filters spectrally distinct in an asymmetric layout.

### Symmetry of the direct filters, and convergence

It is worth pointing out that the FIR filters for the direct components of each channel are identical — before the balance is applied — despite the system being asymmetric, since both depend only on $P$, which is symmetric under the exchange of channels. The asymmetry is reflected in the cross FIR filters, which differ in the factors $G_l$ and $G_r$.

The convergence condition in this asymmetric case is that the magnitude of the product $G_l \cdot G_r$ stays below unity at all frequencies:

```math
\left | G_l(e^{j\omega}) \cdot G_r(e^{j\omega}) \right | < 1 \quad \forall \omega
```

In the usual NatAmbio parametrization both functions represent attenuated crossed paths, so this condition is normally satisfied. Note that the condition now applies to the product and not to each term separately: the asymmetry allows one of the two crossed paths to be less attenuated than the other, as long as the product remains bounded.

The factor $b$ lies outside the recursive cancellation loop and therefore does not modify its convergence condition. Compensating for it consists in applying a gain $1/b$ to both the direct and the cross component destined for the right channel, which is exactly the effect of pre-multiplying $\mathbf{M}^{-1}$ by $\mathbf{D}^{-1}$. The choice of reference channel is arbitrary: the system could equally be normalised with respect to $H_{rr}$, applying the inverse factor to the left channel.

## Setting the balance between channels

In NatAmbio the matrix $\mathbf{D}^{-1}$ is not baked into the generated coefficients: it is realised as a gain in the convolution routing, set by the user. The filter generator produces $\mathbf{M}^{-1}$ only. The reason is that the balance is not a parameter of the acoustic model but a level adjustment of the particular system, and one that has to be made by hand — measuring or listening at the listening position — regardless of where the value lives.

Since $\mathbf{D}^{-1}$ pre-multiplies $\mathbf{M}^{-1}$, it scales a whole row of the filtering matrix, that is, **everything delivered to one loudspeaker**: both its direct and its cross component. In practice this means applying the same gain to the two convolutions feeding the same output, no matter which input each of them comes from.

### Attenuate, never amplify

The strict factor is $1/b$ on the right channel which, if $b<1$, means a boost and with it a loss of headroom and a risk of clipping. Multiplying the whole filtering matrix by $b$ is, however, equivalent as far as cancellation is concerned — only the ratio between the two gains matters — and yields $\mathrm{diag}(b, 1)$, that is, an attenuation of the left channel. The practical rule is therefore to **attenuate the channel that arrives louder at the listening position and leave the other one untouched**, never the other way round.

### Proposed procedure

The adjustment can be made by ear, with no instrumentation:

1. Play a **mono signal through both channels**, so that the resulting image must be perceived exactly centred. Any monophonic material will do, or a stereo recording summed to mono.
2. Progressively attenuate whichever channel is perceived as dominant — the two convolutions feeding that output, by the same amount — until the image sits in the centre.
3. Consider the adjustment done at that point. The ear's resolution for centring a mono phantom source is of the order of a tenth of a dB under favourable conditions and, in any case, comfortably better than the target justified below.

It should be noted that if the DRC stage already levels both channels against a common target, the residual balance is practically zero and the adjustment stays at 0 dB. The procedure is there for when that is not the case.

### Why the balance is not a cosmetic adjustment

One might think that a badly set balance only degrades the tonal equilibrium or the position of the image, leaving crosstalk cancellation intact. That is not the case. If $\mathbf{D}^{-1}$ is not applied, the effective filtering is $\mathbf{M}^{-1}$ instead of $\mathbf{D}^{-1}\mathbf{M}^{-1}$, and the result over the acoustic path is:

```math
\mathbf{H} \cdot \mathbf{M}^{-1} = \frac{H_{ll}}{1-P} \begin{bmatrix} 1-bP & G_r(b-1) \\ G_l(1-b) & b-P \end{bmatrix}
```

The crossed terms **do not vanish**: they remain proportional to $(b-1)$. In other words, the balance is part of the cancellation, not something added on top of it. This is perfectly consistent with what was said earlier — $b$ lies outside the *recursive loop* and does not affect convergence — but being outside the loop does not make it optional.

### Cancellation ceiling

The limit imposed by an imperfect balance follows from the expression above. The residual crosstalk relative to the direct signal is, to first order, $|1-b|$ times what it was before filtering, so the achievable cancellation is bounded by:

```math
\text{cancellation}_{max} \approx 20 \log_{10} \left| 1 - b \right| \ \text{dB}
```

| Balance error | Cancellation ceiling |
|---|---|
| 0.5 dB | ≈ −25 dB |
| 1 dB | ≈ −19 dB |
| 2 dB | ≈ −14 dB |
| 3 dB | ≈ −11 dB |
| 6 dB | ≈ −6 dB |

The practical reading is that **setting the balance to within less than 1 dB places the ceiling at about 20 dB of cancellation**, which is of the same order as what the parametric model itself can deliver; refining it further adds nothing, whereas leaving a 3 dB error limits the system to about 11 dB, well below its potential. This is what turns "adjust it by ear" into a target with a defined tolerance, and what justifies the procedure of the previous section being sufficient.

## Low-frequency protection

As in the symmetric case, the mathematical convergence of the series does not by itself guarantee optimal acoustic behaviour. Below roughly 200 Hz the level difference between direct and crossed reception shrinks considerably — the head stops casting an acoustic shadow at large wavelengths — so that $|G_l|$ and $|G_r|$ approach unity. Even though their product stays below 1 and the series converges, the interaction of the cross filters with the modal response of the room and with the loudspeakers' own response can produce an audible reinforcement in the bass, perceived not as an XTC effect but as an unwanted, acoustically resonant boost.

The same protection as in the symmetric model is therefore applied: XTC action is bounded below **200 Hz**, attenuating the level with a **6 dB/octave** roll-off. It is built into the $ILD_{spectrum}$ model itself, so it affects $S_l$, $S_r$ and $\bar{S}$ alike, and therefore both cross filters and the corrective terms of the direct one. The $\delta$ component of the direct filter is unaffected, so that at low frequency the direct filter tends to unity and the system converges smoothly to unprocessed stereo.

Measured on the filters actually generated for an example asymmetric layout (left 180 µs / 10 dB / 20°, right 140 µs / 8 dB / 15°, 4096 taps at 48 kHz), both cross filters show a slope of about 6 dB/octave below 200 Hz while the direct filter approaches 0 dB. The low-frequency behaviour is therefore the same as in the symmetric case.

## Listening validation

The model has been tested on a real system, with a result worth documenting both for what it confirms and for what it reframes.

### The case

Loudspeakers in a symmetric layout — same ITD and same azimuth on both sides — but with the two crossed paths clearly different: tuning by ear converges to $\text{ILD}_l = 21$ dB and $\text{ILD}_r = 12$ dB, nine decibels apart. The balance resulting from the procedure described above is 0 dB; with only 1 dB of attenuation on the right channel the mono image already shifted perceptibly to the left.

### A null balance is consistent with the model

$b = H_{rr}/H_{ll}$ is the ratio of the **direct** paths, whereas the ILDs parametrise the **crossed** ones: they are independent quantities. With the DRC stage levelling both direct paths against a common target, $b \approx 1$ is to be expected however different the crosstalk may be on each side. In this system the asymmetry lives entirely in $\mathbf{M}$ and not at all in $\mathbf{D}$, which is the opposite limiting case to the one that motivates the balance section. Incidentally, the sensitivity observed — 1 dB clearly audible on a mono image — confirms that the listening procedure comfortably resolves the tolerance target that justifies the cancellation-ceiling table.

### Why the symmetric compromise was expensive

Before the asymmetric model was available, the system ran with a single ILD of 16 dB, a compromise value. With 12 dB on both sides one of them behaved excellently — the virtual scene opened beyond 70° of azimuth — while the other collapsed, failing to reach 40°.

The relevant observation is that **the failure is not symmetric**. Falling short of full cancellation merely narrows the scene; overshooting destroys it, because the corrective signal exceeds the actual crosstalk and the residual, inverted and shifted in time, introduces a localisation cue that corresponds to no source at all. A single parameter is therefore held hostage by the worse of the two sides: it cannot be as aggressive as the good side allows without breaking the other. The 16 dB compromise was not splitting the error evenly; it was giving up most of the attainable width on one side in order to avoid collapse on the other. With the two ILDs independent, the scene turned out not only wider but **stable**, which is the signature of a cancellation correctly matched in both time and level.

This is, in practice, the strongest argument for the asymmetric model, and it is independent of the balance: even with $b = 1$ exactly, a single $G$ forces one to aim low.

### Likely origin, and the ceiling it implies

The listener's hypothesis, consistent with the geometry of the room, is that the difference comes from early reflections, different to left and right because of the room boundaries and the furniture. The mechanism is plausible on signal-margin grounds: the direct path is the strongest arrival at the ipsilateral ear and a reflection several decibels below it barely perturbs it, whereas the crossed path arrives already attenuated by the head shadow, so a reflection reaching the contralateral ear without suffering that shadow can rival it in level. The same room asymmetry alters $G_l$ and $G_r$ far more than it alters $H_{ll}$ and $H_{rr}$ — and what little it does alter in the latter is corrected by the DRC.

This bounds what can be expected. A reflection is a temporal phenomenon and the $ILD_{avg}$ parameter can only absorb it as level: the model cancels a single delayed copy and cannot cancel a second arrival at a different delay. The value that tuning by ear converges to is then a compromise between cancelling the direct crosstalk and not worsening the residual against the reflection, and the ceiling this imposes lies not in the parameters but in the room. Consistently with this, the improvement reported over the symmetric compromise is described as real but moderate: in such a system the next step up is not finer XTC tuning but treating the first reflection point.

### Scope of this validation

This is a single system and a subjective adjustment, with no instrumental verification of the reflection hypothesis — which would be confirmed by measuring the binaural response at the listening position and comparing the first arrivals of each crossed path. What is established is that the asymmetric model covers a use case the original motivation of this note did not contemplate: not asymmetry of placement, but asymmetry of the acoustic environment with the loudspeakers symmetrically placed.

## Reduction to the symmetric case

The main equations of the XTC technical note are readily obtained from those of the asymmetric model by simply setting:

```math
g_l = g_r = g
```

and

```math
S_l = S_r = \bar{S} = S
```

which gives

```math
P = g^2 \ast S
```

and reduces the expressions term by term, for the same $N$, to those of the symmetric case:

```math
F^{direct} = \delta + \sum_{i=1}^{N} P^{i}
```

and

```math
F^{cross} = -G \ast \sum_{i=1}^{N} P^{i-1}
```

whose term $i$ is

```math
g^{2i} \ast S^{i}
```

and

```math
g^{2i-1} \ast S^{i}
```

respectively. This equivalence is what the `make check` test in `lib/` verifies, requiring the asymmetric generator to reproduce the symmetric one when both sides carry the same parameters.

Although the model covers it, an XTC implementation in asymmetric environments cannot reasonably be expected to reach the performance of an equivalent symmetric layout, so it will always be advisable to arrange a stereo setup that is, if not standard, at least symmetric.
