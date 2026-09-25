/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef DSP_H
#define DSP_H

/* Largest transform length these routines will handle. Keeps len_a + len_b - 1
 * and its next-power-of-2 padding inside int range. Exposed because callers
 * that size their own transform (the fractional-ITD XTC recursions) must be
 * able to range-check the length they are about to ask for. */
#define DSP_MAX_LEN (1 << 26)

/* Callback: target gain in dB for a physical frequency in Hz.
 * Only invoked for 0 < f_hz < Nyquist (the endpoints are forced to 0
 * inside firwin2 without calling the model). */
typedef double (*firwin2_db_model_fn)(double f_hz, void *ctx);

/* firwin2 — linear-phase Type II FIR design by evaluating a dB model
 * directly on the internal uniform grid (no intermediate (freq, gain)
 * table or interpolation).
 *
 *   numtaps     : output filter length
 *   sample_rate : Hz; maps grid index → physical frequency
 *   model       : callback returning target gain in dB
 *   ctx         : opaque context passed to model
 *   out         : output buffer (numtaps doubles, allocated by the caller)
 *
 * Implementation: uniform grid of size 1 + 2^ceil(log2(numtaps)) over
 * [0, Nyquist], dB→linear conversion, linear phase shift, real IFFT via
 * FFTW (c2r), truncation, and symmetric Hamming window.
 *
 * The endpoints f=0 and f=Nyquist are forced to gain 0: the latter because
 * Type II (even numtaps) requires it; the former for symmetry and to avoid
 * the model having to handle log(0).
 *
 * Returns 0 on success, non-zero on error.
 */
int firwin2(int numtaps, int sample_rate,
            firwin2_db_model_fn model, void *ctx,
            double *out);

/* What firwin2_ex() does with the two grid endpoints. firwin2() forces both,
 * which is what it has always done and what a Type II design requires at
 * Nyquist; these let a caller that knows better ask for the model's own value
 * there instead.
 *
 * It matters for one shape in particular. A HIGH-PASS has its pass band AT
 * Nyquist, so a design that forces zero there is not a high-pass with a small
 * blemish, it is a band-pass nobody asked for -- and an even numtaps cannot be
 * rescued by the flag either, a Type II response being zero at Nyquist by
 * construction whatever the target says. A high-pass therefore needs an ODD
 * numtaps and DSP_FIRWIN2_ZERO_NYQ clear, both.
 *
 * At DC the flag is a convenience rather than a constraint: a model with a
 * skirt going down to zero frequency returns -inf there, which firwin2_ex
 * rejects along with any other non-finite value, so a caller either floors its
 * model or sets the flag and is spared the question. */
#define DSP_FIRWIN2_ZERO_DC    (1u << 0)
#define DSP_FIRWIN2_ZERO_NYQ   (1u << 1)

/* firwin2_ex — firwin2 with the endpoints under the caller's control.
 *
 * Identical in every other respect, and firwin2() is now this with both flags
 * set: same grid, same phase shift, same Hamming window, same results to the
 * bit for the callers that were there before.
 */
int firwin2_ex(int numtaps, int sample_rate,
               firwin2_db_model_fn model, void *ctx,
               unsigned flags, double *out);

/* firwin2_deviation — how far a FINISHED filter's magnitude sits from the model
 * it was meant to have, in dB.
 *
 *   h           : the filter, as it will actually be applied
 *   n           : its length
 *   sample_rate : Hz
 *   model, ctx  : the same model the design was asked for
 *   f_lo, f_hi  : the band the comparison is made over. Outside it a filter is
 *                 not being judged at all: a linear-phase FIR has a frequency
 *                 resolution of about fs/n and simply cannot shape anything
 *                 below it, so a template evaluated at 9 Hz reports a failure
 *                 that is arithmetic rather than audible. 20 Hz to 20 kHz is
 *                 the band that means something here.
 *   floor_db    : below this the model is not taken literally. Where the target
 *                 is at or under the floor, only an EXCESS counts -- a filter
 *                 that reaches 20 dB deeper than asked has not failed at
 *                 anything, and made to count, that surplus would be the whole
 *                 answer and would hide the pass band's real error.
 *   max_dev_db  : the worst deviation found
 *   at_hz       : where it was found, which is usually the corner
 *
 * It exists because <length> is the caller's to choose and nothing else tells
 * them whether they chose enough. A filter too short for the slope it was asked
 * for does not fail: it comes out as a gentler filter than the file describes,
 * and there is nothing downstream that can notice. This is the number that
 * notices, and it is measured on the applied response -- after the conversion
 * to minimum phase, where there is one -- so it also catches a cepstrum that
 * did not converge.
 *
 * DC and Nyquist are skipped. Both are endpoints the design may legitimately
 * have forced, and a model with a skirt reaching zero frequency has no value
 * there to compare against.
 *
 * Returns 0 on success, non-zero on error.
 */
int firwin2_deviation(const double *h, int n, int sample_rate,
                      firwin2_db_model_fn model, void *ctx,
                      double f_lo, double f_hi, double floor_db,
                      double *max_dev_db, double *at_hz);

/* butterworth_fir — the classical cascade, realised as an FIR by running a
 * delta through it.
 *
 * No transform anywhere. The poles come out of the closed form -- for an order
 * N section pair, Q_k = 1 / (2 sin((2k-1)pi/2N)) -- the bilinear transform with
 * prewarping at the corner turns each into a biquad, and the impulse response
 * is what a delta leaves on the way out. The result is minimum phase and stable
 * by construction rather than by a reconstruction that has to be checked:
 * nothing here can fail to converge, because nothing here iterates.
 *
 *   order_hp, fc_hp_hz : the high-pass cascade. order 0 means none.
 *   order_lp, fc_lp_hz : the low-pass cascade. order 0 means none.
 *
 * Both together is a band-pass, and the two skirts keep their own slopes. Each
 * order is 6 dB/octave, which is the one thing this cannot do that a sampled
 * magnitude can: a slope that is not a multiple of six has no integer order and
 * the caller has to go the other way.
 *
 * The only approximation is the truncation at n taps. The response decays like
 * the cascade does, so a corner low against the sample rate needs a long one --
 * firwin2_deviation() is how a caller finds out whether it gave enough.
 *
 * Returns 0 on success, non-zero on error.
 */
int butterworth_fir(int order_hp, double fc_hp_hz,
                    int order_lp, double fc_lp_hz,
                    int sample_rate, int n, double *out);

/* minimum_phase — minimum-phase reconstruction via homomorphic cepstrum.
 *
 *   x   : input (length n)
 *   n   : length
 *   out : output (length n, allocated by the caller)
 *
 * Algorithm, carried out on a transform of length N = next_pow2(n) · 8, with x
 * zero-padded, and truncated back to n taps at the end:
 *   X      = FFT(x, N)
 *   c      = IFFT(log|X| + eps).real
 *   c_min  = c · window_min      (causal fold: 1, 2..2, 1, 0..0)
 *   y      = IFFT(exp(FFT(c_min))).real
 *
 * The oversampling is not optional. The complex cepstrum has infinite support
 * and decays as ~1/k, so a transform of length n aliases its tail back onto
 * itself; see the comment above the definition in dsp.c for the measured cost.
 *
 * Returns 0 on success, non-zero on error.
 */
int minimum_phase(const double *x, int n, double *out);

/* dsp_next_pow2 — smallest power of two >= n, or 0 if n is outside
 * [1, DSP_MAX_LEN]. Every caller must treat 0 as an error. */
int dsp_next_pow2(int n);

/* dsp_rfft / dsp_irfft — real half-spectrum transforms.
 *
 * The pair exists for callers that need to work on the spectrum itself rather
 * than convolve two impulse responses: the fractional-ITD XTC recursions
 * accumulate taps as linear-phase factors, which has no time-domain equivalent
 * that does not first quantise the delay.
 *
 * The half spectrum is carried as two separate real arrays of nfft/2+1 doubles
 * so that this header stays free of any FFTW type; the transforms are the usual
 * r2c / c2r pair underneath.
 *
 *   dsp_rfft  : `in` (n_in samples) zero-padded to nfft, forward transform.
 *   dsp_irfft : inverse of a half spectrum, carrying the 1/nfft normalisation,
 *               copied into out_len samples (truncated, or zero-padded if
 *               out_len > nfft).
 *
 * nfft must be even, at least 2, and at most DSP_MAX_LEN. A real signal's
 * Nyquist bin is real; a caller that builds a spectrum by hand is responsible
 * for leaving im[nfft/2] at zero, and dsp_irfft ignores whatever is there.
 *
 * Return 0 on success, non-zero on error.
 */
int dsp_rfft(const double *in, int n_in, int nfft, double *re, double *im);
int dsp_irfft(const double *re, const double *im, int nfft,
              double *out, int out_len);

/* dsp_spectrum_add_delayed — acc += gain * exp(-j*2*pi*f*delay), over the whole
 * half spectrum (nfft/2+1 bins). `delay` is in samples and may be fractional:
 * this is how the XTC recursions place a tap without quantising the ITD.
 *
 * The Nyquist bin is forced real. A real, even-length signal has no imaginary
 * part there, which a fractional shift would otherwise violate; keeping the
 * real part is the standard resolution and yields the periodic-sinc
 * interpolator. The error is confined to that single bin, and in the XTC
 * pipeline the ILD shelf has already taken |A| down to ~0.02 by then.
 *
 * dsp_spectrum_mul — acc *= m, pointwise complex over the same half spectrum.
 * Together they are one iteration of a Horner recursion carried out on spectra.
 *
 * Return 0 on success, non-zero on error.
 */
int dsp_spectrum_add_delayed(double *re, double *im, int nfft,
                             double gain, double delay);
int dsp_spectrum_mul(double *re, double *im,
                     const double *mre, const double *mim, int nfft);

/* fft_convolve_truncate — C equivalent of scipy.signal.fftconvolve(a, b)[:out_len].
 *
 * Linear convolution of a (len_a) and b (len_b) via FFT with zero-padding to
 * the next power of 2 ≥ len_a + len_b - 1, truncated to out_len samples.
 */
int fft_convolve_truncate(const double *a, int len_a,
                          const double *b, int len_b,
                          double *out, int out_len);

#endif
