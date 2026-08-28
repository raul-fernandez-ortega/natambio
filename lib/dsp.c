/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "dsp.h"

#include <fftw3.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* This unit uses the double-precision FFTW API (fftw_*), so the binary links
 * -lfftw3 in addition to the -lfftw3f that zita-convolver already requires.
 *
 * The extra dependency buys correctness in minimum_phase(). The homomorphic
 * cepstrum takes log|X|, and an even-length (Type II) linear-phase FIR from
 * firwin2() has a forced zero at Nyquist: the symmetry makes sum h[k]*(-1)^k
 * vanish whatever the coefficients are. At that bin |X| collapses onto the FFT's
 * own noise floor, which in single precision sits around 1e-5 -- seven orders of
 * magnitude above LOG_EPS. log|X| then returned the log of the rounding error
 * instead of the log of the response, and get_xtc() fed that noise through
 * sixteen chained convolutions. In double precision the noise floor drops below
 * LOG_EPS and the epsilon does the job it was written to do.
 *
 * (DC is *not* a forced zero, despite the model being pinned to 0 there: the
 * Hamming window smears it back to a finite value. Only Nyquist is structural,
 * and only for even numtaps.)
 *
 * Filter *coefficients* stay single precision: naconf.cpp narrows to float when
 * it hands them to zita-convolver, which is the right place for the conversion
 * and is plenty for playback. Only the design math is double.
 *
 * These routines run offline at configuration time, never on the RT path, so the
 * cost of the wider transform is irrelevant.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LOG_EPS 1e-12

/* DSP_MAX_LEN is defined in dsp.h: it bounds any transform length handled here,
 * keeping len_a + len_b - 1 and its next-power-of-2 padding inside int range so
 * next_pow2() cannot spin past INT_MAX and the shift below stays defined. It is
 * public because callers that size their own transform need the same bound. */

/* Oversampling factor for the cepstral transform in minimum_phase(); see the
 * comment on that function. Measured on the XTC ILD filter (4096 taps, 48 kHz):
 * a factor of 4 already drives the magnitude error below 0.001 dB, so 8 is a
 * margin, not a requirement. It costs nothing: this runs offline. */
#define MP_CEPSTRUM_OVERSAMPLE 8

/* Smallest power of two >= n. Returns 0 if n is out of range, which every caller
 * treats as an error: without the cap, p <<= 1 would shift into the sign bit and
 * loop forever. */
static int next_pow2(int n) {
    if (n < 1 || n > DSP_MAX_LEN) return 0;
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

int firwin2(int numtaps, int sample_rate,
            firwin2_db_model_fn model, void *ctx,
            double *out) {
    if (numtaps < 2 || numtaps > DSP_MAX_LEN) return -1;
    if (sample_rate <= 0 || !model || !out)   return -1;

    /* nfreqs = 1 + 2^ceil(log2(numtaps))  →  half-complex grid */
    int log2nt = 0;
    int v = numtaps;
    while (v > 1) { v >>= 1; log2nt++; }
    if ((1 << log2nt) < numtaps) log2nt++;
    int nfreqs    = 1 + (1 << log2nt);
    int real_size = 2 * (nfreqs - 1);          /* real IFFT length */

    fftw_complex *spec = fftw_alloc_complex((size_t)nfreqs);
    double       *time = fftw_alloc_real((size_t)real_size);
    if (!spec || !time) {
        if (spec) fftw_free(spec);
        if (time) fftw_free(time);
        return -2;
    }
    fftw_plan plan = fftw_plan_dft_c2r_1d(real_size, spec, time, FFTW_ESTIMATE);
    if (!plan) {
        fftw_free(spec);
        fftw_free(time);
        return -3;
    }

    /* Evaluate the model directly at each grid bin and build the target
     * half-spectrum with a linear phase shift:
     *   x[i]        = i / (nfreqs - 1)              ∈ [0, 1]
     *   f_hz[i]     = x[i] · sample_rate / 2
     *   gain_db[i]  = model(f_hz[i], ctx)
     *   spec[i]     = 10^(gain_db/20) · exp(-j · π · x[i] · (numtaps-1)/2)
     *
     * Endpoints i=0 (DC) and i=nfreqs-1 (Nyquist) are forced to 0 without
     * calling the model: the latter because Type II requires it, the former
     * for consistency and to spare the callback from handling log(0).
     *
     * The absolute scale of the response is irrelevant: the RMS normalisation
     * applied after firwin2 absorbs any constant dB offset. */
    double nyq_hz = 0.5 * (double)sample_rate;
    double delay  = 0.5 * (double)(numtaps - 1);
    for (int i = 0; i < nfreqs; i++) {
        double gain_lin;
        if (i == 0 || i == nfreqs - 1) {
            gain_lin = 0.0;
        } else {
            double f_hz = ((double)i / (double)(nfreqs - 1)) * nyq_hz;
            double db   = model(f_hz, ctx);
            /* A model that returns a non-finite or absurd dB value would turn
             * the whole spectrum into inf/NaN on the pow() below. Reject it
             * here, where the offending bin is still identifiable. */
            if (!isfinite(db) || db > 300.0) {
                fftw_destroy_plan(plan);
                fftw_free(spec);
                fftw_free(time);
                return -4;
            }
            gain_lin = pow(10.0, db / 20.0);
        }
        double x     = (double)i / (double)(nfreqs - 1);
        double phase = -M_PI * x * delay;
        spec[i][0]   = gain_lin * cos(phase);
        spec[i][1]   = gain_lin * sin(phase);
    }

    fftw_execute(plan);

    /* FFTW c2r is unnormalised (÷ real_size); symmetric Hamming window
     * (fftbins=False):  w[n] = 0.54 - 0.46 · cos(2πn / (N-1)) */
    double norm = 1.0 / (double)real_size;
    for (int n = 0; n < numtaps; n++) {
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * (double)n / (double)(numtaps - 1));
        out[n]   = time[n] * norm * w;
    }

    fftw_destroy_plan(plan);
    fftw_free(spec);
    fftw_free(time);
    return 0;
}

/* The cepstral transforms run on N = next_pow2(n) * MP_CEPSTRUM_OVERSAMPLE, not
 * on n. The complex cepstrum of a FIR has infinite support and decays only as
 * ~1/k, so an n-point transform wraps its tail back onto itself: classic
 * time-domain aliasing. The folded cepstrum is then no longer the causal part of
 * the true one, and exp() turns that error into a magnitude error.
 *
 * Measured on the XTC ILD filter (4096 taps, 48 kHz, az 27 deg, alpha 2), as the
 * deviation of |H_minphase| from the |H| of the linear-phase FIR it must match,
 * over 20 Hz - 20 kHz:
 *
 *     transform length      max error      rms error
 *     n (no padding)         0.200 dB       0.051 dB
 *     4n                     0.0002 dB      0.00003 dB
 *
 * That 0.2 dB looks harmless in isolation, and it is -- but get_xtc() applies
 * this filter through sixteen chained convolutions, which amplified it to a ~5%
 * relative error in the delivered XTC filters. At even n the effect is worse
 * still: a Type II FIR has H(Nyquist) = 0 identically (the symmetry forces
 * sum h[k]*(-1)^k = 0 regardless of the coefficients), so log|X| hits the
 * LOG_EPS floor at a bin that carries real weight when the grid is only n long.
 * Padding dilutes that single bin across a much finer grid, which is why it
 * fixes the even and odd cases alike -- filter_len parity no longer matters.
 *
 * The padded transform is a power of two, hence always even, so only the even
 * branch of the cepstral fold is reachable.
 */
int minimum_phase(const double *x, int n, double *out) {
    if (n < 2 || n > DSP_MAX_LEN) return -1;
    if (!x || !out)               return -1;

    /* N = next_pow2(n) * oversample, clamped so it stays within DSP_MAX_LEN.
     * A filter long enough to hit the clamp gets less padding, not none. */
    int N = next_pow2(n);
    if (N == 0) return -1;
    for (int f = 1; f < MP_CEPSTRUM_OVERSAMPLE && N <= DSP_MAX_LEN / 2; f *= 2) {
        N <<= 1;
    }
    int half = N / 2 + 1;

    fftw_complex *X        = fftw_alloc_complex((size_t)half);
    double       *real_buf = fftw_alloc_real((size_t)N);
    double       *ceps     = fftw_alloc_real((size_t)N);
    double       *out_buf  = fftw_alloc_real((size_t)N);
    if (!X || !real_buf || !ceps || !out_buf) {
        if (X)        fftw_free(X);
        if (real_buf) fftw_free(real_buf);
        if (ceps)     fftw_free(ceps);
        if (out_buf)  fftw_free(out_buf);
        return -2;
    }

    /* FFTW_ESTIMATE does not destroy the buffers during planning,
     * so we can create all plans upfront. */
    fftw_plan p_fwd1 = fftw_plan_dft_r2c_1d(N, real_buf, X,       FFTW_ESTIMATE);
    fftw_plan p_inv1 = fftw_plan_dft_c2r_1d(N, X,        ceps,    FFTW_ESTIMATE);
    fftw_plan p_fwd2 = fftw_plan_dft_r2c_1d(N, ceps,     X,       FFTW_ESTIMATE);
    fftw_plan p_inv2 = fftw_plan_dft_c2r_1d(N, X,        out_buf, FFTW_ESTIMATE);
    if (!p_fwd1 || !p_inv1 || !p_fwd2 || !p_inv2) {
        if (p_fwd1) fftw_destroy_plan(p_fwd1);
        if (p_inv1) fftw_destroy_plan(p_inv1);
        if (p_fwd2) fftw_destroy_plan(p_fwd2);
        if (p_inv2) fftw_destroy_plan(p_inv2);
        fftw_free(X); fftw_free(real_buf); fftw_free(ceps); fftw_free(out_buf);
        return -3;
    }

    /* 1) X = FFT(x zero-padded to N) */
    memset(real_buf, 0, (size_t)N * sizeof(double));
    for (int k = 0; k < n; k++) real_buf[k] = x[k];
    fftw_execute(p_fwd1);

    /* 2) X ← log|X| + 0j  (keep valid half-complex format).
     * hypot() rather than sqrt(re*re + im*im): the squares would underflow to
     * zero at the Nyquist zero of an even-length FIR, which is exactly where
     * this epsilon is supposed to be the thing that saves us. */
    for (int k = 0; k < half; k++) {
        double mag = hypot(X[k][0], X[k][1]) + LOG_EPS;
        X[k][0] = log(mag);
        X[k][1] = 0.0;
    }

    /* 3) ceps = IFFT(log|X|).real  (÷ N to normalise) */
    fftw_execute(p_inv1);
    double inv_N = 1.0 / (double)N;
    for (int k = 0; k < N; k++) ceps[k] *= inv_N;

    /* 4) Fold the cepstrum onto its causal part:  [1, 2, 2, …, 2, 1, 0, …, 0].
     * N is a power of two, so it is always even. */
    for (int k = 1; k < N / 2; k++)     ceps[k] *= 2.0;
    for (int k = N / 2 + 1; k < N; k++) ceps[k]  = 0.0;

    /* 5) X = FFT(ceps · window)  → complex cepstrum of the min-phase signal */
    fftw_execute(p_fwd2);

    /* 6) X ← exp(X)   :  exp(a + jb) = e^a · (cos b + j sin b) */
    for (int k = 0; k < half; k++) {
        double a  = X[k][0];
        double b  = X[k][1];
        double ea = exp(a);
        X[k][0] = ea * cos(b);
        X[k][1] = ea * sin(b);
    }

    /* 7) out = IFFT(exp(...)).real, truncated to n taps. The min-phase response
     * concentrates its energy at the head, so the discarded tail is negligible:
     * it is what keeps the 4n magnitude error at 0.0002 dB above. */
    fftw_execute(p_inv2);
    for (int k = 0; k < n; k++) out[k] = out_buf[k] * inv_N;

    fftw_destroy_plan(p_fwd1);
    fftw_destroy_plan(p_inv1);
    fftw_destroy_plan(p_fwd2);
    fftw_destroy_plan(p_inv2);
    fftw_free(X);
    fftw_free(real_buf);
    fftw_free(ceps);
    fftw_free(out_buf);
    return 0;
}

int fft_convolve_truncate(const double *a, int len_a,
                          const double *b, int len_b,
                          double *out, int out_len) {
    if (!a || !b || !out)              return -1;
    if (len_a < 1 || len_b < 1)        return -1;
    if (out_len < 1)                   return -1;
    /* len_a + len_b - 1 must not overflow int, and the padded length must stay
     * within what next_pow2 can represent. */
    if (len_a > DSP_MAX_LEN || len_b > DSP_MAX_LEN) return -1;

    int conv_len = len_a + len_b - 1;
    int N        = next_pow2(conv_len);
    if (N == 0)  return -1;
    int half     = N / 2 + 1;

    double       *ap     = fftw_alloc_real((size_t)N);
    double       *bp     = fftw_alloc_real((size_t)N);
    fftw_complex *Af     = fftw_alloc_complex((size_t)half);
    fftw_complex *Bf     = fftw_alloc_complex((size_t)half);
    double       *result = fftw_alloc_real((size_t)N);
    if (!ap || !bp || !Af || !Bf || !result) {
        if (ap)     fftw_free(ap);
        if (bp)     fftw_free(bp);
        if (Af)     fftw_free(Af);
        if (Bf)     fftw_free(Bf);
        if (result) fftw_free(result);
        return -2;
    }

    memset(ap, 0, (size_t)N * sizeof(double));
    memset(bp, 0, (size_t)N * sizeof(double));
    memcpy(ap, a, (size_t)len_a * sizeof(double));
    memcpy(bp, b, (size_t)len_b * sizeof(double));

    fftw_plan pa = fftw_plan_dft_r2c_1d(N, ap, Af,     FFTW_ESTIMATE);
    fftw_plan pb = fftw_plan_dft_r2c_1d(N, bp, Bf,     FFTW_ESTIMATE);
    fftw_plan pi = fftw_plan_dft_c2r_1d(N, Af, result, FFTW_ESTIMATE);
    if (!pa || !pb || !pi) {
        if (pa) fftw_destroy_plan(pa);
        if (pb) fftw_destroy_plan(pb);
        if (pi) fftw_destroy_plan(pi);
        fftw_free(ap); fftw_free(bp); fftw_free(Af); fftw_free(Bf); fftw_free(result);
        return -3;
    }

    fftw_execute(pa);
    fftw_execute(pb);

    /* Point-wise multiplication in the frequency domain,
     * with the 1/N IFFT normalisation folded into Af. */
    double inv_N = 1.0 / (double)N;
    for (int k = 0; k < half; k++) {
        double ar = Af[k][0], ai = Af[k][1];
        double br = Bf[k][0], bi = Bf[k][1];
        Af[k][0] = (ar * br - ai * bi) * inv_N;
        Af[k][1] = (ar * bi + ai * br) * inv_N;
    }

    fftw_execute(pi);

    int copy_len = (out_len < N) ? out_len : N;
    memcpy(out, result, (size_t)copy_len * sizeof(double));
    for (int k = copy_len; k < out_len; k++) out[k] = 0.0;

    fftw_destroy_plan(pa);
    fftw_destroy_plan(pb);
    fftw_destroy_plan(pi);
    fftw_free(ap);
    fftw_free(bp);
    fftw_free(Af);
    fftw_free(Bf);
    fftw_free(result);
    return 0;
}

int dsp_next_pow2(int n) {
    return next_pow2(n);
}

/* Shared preconditions of the two half-spectrum transforms. nfft must be even
 * because the r2c/c2r pair is only its own inverse on an even frame, and the
 * Nyquist bin the callers manipulate only exists there. */
static int rfft_len_ok(int nfft) {
    return nfft >= 2 && nfft <= DSP_MAX_LEN && (nfft % 2) == 0;
}

int dsp_rfft(const double *in, int n_in, int nfft, double *re, double *im) {
    if (!in || !re || !im)     return -1;
    if (n_in < 1 || n_in > nfft) return -1;
    if (!rfft_len_ok(nfft))    return -1;

    const int half = nfft / 2 + 1;

    double       *buf  = fftw_alloc_real((size_t)nfft);
    fftw_complex *spec = fftw_alloc_complex((size_t)half);
    if (!buf || !spec) {
        if (buf)  fftw_free(buf);
        if (spec) fftw_free(spec);
        return -2;
    }

    memset(buf, 0, (size_t)nfft * sizeof(double));
    memcpy(buf, in, (size_t)n_in * sizeof(double));

    fftw_plan p = fftw_plan_dft_r2c_1d(nfft, buf, spec, FFTW_ESTIMATE);
    if (!p) {
        fftw_free(buf);
        fftw_free(spec);
        return -3;
    }
    fftw_execute(p);

    for (int k = 0; k < half; k++) {
        re[k] = spec[k][0];
        im[k] = spec[k][1];
    }
    /* DC and Nyquist of a real signal are real. FFTW already returns zero there;
     * setting it explicitly keeps that an invariant the callers can rely on
     * rather than a property of the library. */
    im[0]        = 0.0;
    im[half - 1] = 0.0;

    fftw_destroy_plan(p);
    fftw_free(buf);
    fftw_free(spec);
    return 0;
}

int dsp_irfft(const double *re, const double *im, int nfft,
              double *out, int out_len) {
    if (!re || !im || !out)  return -1;
    if (out_len < 1)         return -1;
    if (!rfft_len_ok(nfft))  return -1;

    const int half = nfft / 2 + 1;

    fftw_complex *spec = fftw_alloc_complex((size_t)half);
    double       *buf  = fftw_alloc_real((size_t)nfft);
    if (!spec || !buf) {
        if (spec) fftw_free(spec);
        if (buf)  fftw_free(buf);
        return -2;
    }

    /* The 1/nfft of the inverse transform is folded in here, as
     * fft_convolve_truncate does, so FFTW's unnormalised c2r comes out scaled. */
    const double inv = 1.0 / (double)nfft;
    for (int k = 0; k < half; k++) {
        spec[k][0] = re[k] * inv;
        spec[k][1] = im[k] * inv;
    }
    /* A real output requires a real DC and Nyquist. Forcing them costs nothing
     * and stops a caller's rounding dust at those two bins from being taken
     * for signal. */
    spec[0][1]        = 0.0;
    spec[half - 1][1] = 0.0;

    fftw_plan p = fftw_plan_dft_c2r_1d(nfft, spec, buf, FFTW_ESTIMATE);
    if (!p) {
        fftw_free(spec);
        fftw_free(buf);
        return -3;
    }
    fftw_execute(p);

    int copy_len = (out_len < nfft) ? out_len : nfft;
    memcpy(out, buf, (size_t)copy_len * sizeof(double));
    for (int k = copy_len; k < out_len; k++) out[k] = 0.0;

    fftw_destroy_plan(p);
    fftw_free(spec);
    fftw_free(buf);
    return 0;
}

int dsp_spectrum_add_delayed(double *re, double *im, int nfft,
                             double gain, double delay) {
    if (!re || !im)          return -1;
    if (!rfft_len_ok(nfft))  return -1;
    if (!isfinite(gain) || !isfinite(delay)) return -1;

    const int    half = nfft / 2 + 1;
    const double n    = (double)nfft;

    for (int k = 0; k < half; k++) {
        /* The phase is reduced modulo nfft before scaling rather than after.
         * k*delay reaches ~1e8 for the frames used here, and handing that
         * straight to cos() would spend a third of the mantissa on argument
         * reduction; fmod on an exactly-representable product costs nothing and
         * keeps the angle inside one turn. */
        const double ang = -2.0 * M_PI * fmod((double)k * delay, n) / n;
        re[k] += gain * cos(ang);
        im[k] += gain * sin(ang);
    }
    im[0]        = 0.0;   /* DC and Nyquist of a real signal are real; see the */
    im[half - 1] = 0.0;   /* header. A fractional shift breaks the latter.     */
    return 0;
}

int dsp_spectrum_mul(double *re, double *im,
                     const double *mre, const double *mim, int nfft) {
    if (!re || !im || !mre || !mim) return -1;
    if (!rfft_len_ok(nfft))         return -1;

    const int half = nfft / 2 + 1;
    for (int k = 0; k < half; k++) {
        const double x = re[k], y = im[k];
        re[k] = x * mre[k] - y * mim[k];
        im[k] = x * mim[k] + y * mre[k];
    }
    return 0;
}
