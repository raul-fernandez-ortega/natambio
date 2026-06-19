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
 * Algorithm:
 *   X      = FFT(x)
 *   c      = IFFT(log|X| + eps).real
 *   c_min  = c · window_min      (folding causal: 1, 2..2, 1?, 0..0)
 *   y      = IFFT(exp(FFT(c_min))).real
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
