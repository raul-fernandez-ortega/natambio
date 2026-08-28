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
