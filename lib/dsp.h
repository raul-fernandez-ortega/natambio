/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef DSP_H
#define DSP_H

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

/* fft_convolve_truncate — C equivalent of scipy.signal.fftconvolve(a, b)[:out_len].
 *
 * Linear convolution of a (len_a) and b (len_b) via FFT with zero-padding to
 * the next power of 2 ≥ len_a + len_b - 1, truncated to out_len samples.
 */
int fft_convolve_truncate(const double *a, int len_a,
                          const double *b, int len_b,
                          double *out, int out_len);

#endif
