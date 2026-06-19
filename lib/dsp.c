#include "dsp.h"

#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* This unit uses exclusively the single-precision FFTW API (fftwf_*),
 * so the binary only needs to link -lfftw3f (the same one already required
 * by zita-convolver) and not -lfftw3. The public signatures remain double
 * for compatibility with callers; the double<->float conversion happens at
 * the boundaries. This is consistent with the rest of the project, which
 * is single-precision (float coeffs, zita-convolver fftwf). */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LOG_EPS 1e-12

static int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

int firwin2(int numtaps, int sample_rate,
            firwin2_db_model_fn model, void *ctx,
            double *out) {
    if (numtaps < 2 || sample_rate <= 0 || !model) return -1;

    /* nfreqs = 1 + 2^ceil(log2(numtaps))  →  half-complex grid */
    int log2nt = 0;
    int v = numtaps;
    while (v > 1) { v >>= 1; log2nt++; }
    if ((1 << log2nt) < numtaps) log2nt++;
    int nfreqs    = 1 + (1 << log2nt);
    int real_size = 2 * (nfreqs - 1);          /* real IFFT length */

    fftwf_complex *spec = fftwf_alloc_complex((size_t)nfreqs);
    float         *time = fftwf_alloc_real((size_t)real_size);
    if (!spec || !time) {
        if (spec) fftwf_free(spec);
        if (time) fftwf_free(time);
        return -2;
    }
    fftwf_plan plan = fftwf_plan_dft_c2r_1d(real_size, spec, time, FFTW_ESTIMATE);
    if (!plan) {
        fftwf_free(spec);
        fftwf_free(time);
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
            gain_lin    = pow(10.0, db / 20.0);
        }
        double x     = (double)i / (double)(nfreqs - 1);
        double phase = -M_PI * x * delay;
        spec[i][0]   = (float)(gain_lin * cos(phase));
        spec[i][1]   = (float)(gain_lin * sin(phase));
    }

    fftwf_execute(plan);

    /* FFTW c2r is unnormalised (÷ real_size); symmetric Hamming window
     * (fftbins=False):  w[n] = 0.54 - 0.46 · cos(2πn / (N-1)) */
    double norm = 1.0 / (double)real_size;
    for (int n = 0; n < numtaps; n++) {
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * (double)n / (double)(numtaps - 1));
        out[n]   = (double)time[n] * norm * w;
    }

    fftwf_destroy_plan(plan);
    fftwf_free(spec);
    fftwf_free(time);
    return 0;
}

int minimum_phase(const double *x, int n, double *out) {
    if (n < 2) return -1;
    int half = n / 2 + 1;

    fftwf_complex *X        = fftwf_alloc_complex((size_t)half);
    float         *real_buf = fftwf_alloc_real((size_t)n);
    float         *ceps     = fftwf_alloc_real((size_t)n);
    float         *out_buf  = fftwf_alloc_real((size_t)n);
    if (!X || !real_buf || !ceps || !out_buf) {
        if (X)        fftwf_free(X);
        if (real_buf) fftwf_free(real_buf);
        if (ceps)     fftwf_free(ceps);
        if (out_buf)  fftwf_free(out_buf);
        return -2;
    }

    /* FFTW_ESTIMATE does not destroy the buffers during planning,
     * so we can create all plans upfront. */
    fftwf_plan p_fwd1 = fftwf_plan_dft_r2c_1d(n, real_buf, X,       FFTW_ESTIMATE);
    fftwf_plan p_inv1 = fftwf_plan_dft_c2r_1d(n, X,        ceps,    FFTW_ESTIMATE);
    fftwf_plan p_fwd2 = fftwf_plan_dft_r2c_1d(n, ceps,     X,       FFTW_ESTIMATE);
    fftwf_plan p_inv2 = fftwf_plan_dft_c2r_1d(n, X,        out_buf, FFTW_ESTIMATE);
    if (!p_fwd1 || !p_inv1 || !p_fwd2 || !p_inv2) {
        if (p_fwd1) fftwf_destroy_plan(p_fwd1);
        if (p_inv1) fftwf_destroy_plan(p_inv1);
        if (p_fwd2) fftwf_destroy_plan(p_fwd2);
        if (p_inv2) fftwf_destroy_plan(p_inv2);
        fftwf_free(X); fftwf_free(real_buf); fftwf_free(ceps); fftwf_free(out_buf);
        return -3;
    }

    /* 1) X = FFT(x) */
    for (int k = 0; k < n; k++) real_buf[k] = (float)x[k];
    fftwf_execute(p_fwd1);

    /* 2) X ← log|X| + 0j  (keep valid half-complex format) */
    for (int k = 0; k < half; k++) {
        double re  = (double)X[k][0];
        double im  = (double)X[k][1];
        double mag = sqrt(re * re + im * im) + LOG_EPS;
        X[k][0] = (float)log(mag);
        X[k][1] = 0.0f;
    }

    /* 3) ceps = IFFT(log|X|).real  (÷ n to normalise) */
    fftwf_execute(p_inv1);
    float inv_n = 1.0f / (float)n;
    for (int k = 0; k < n; k++) ceps[k] *= inv_n;

    /* 4) Apply the min-phase window to the cepstrum:
     *      n even : [1, 2, 2, …, 2, 1, 0, 0, …, 0]   (length n)
     *      n odd  : [1, 2, 2, …, 2, 0, 0, …, 0]
     */
    if ((n % 2) == 0) {
        for (int k = 1; k < n / 2; k++) ceps[k] *= 2.0f;
        for (int k = n / 2 + 1; k < n; k++) ceps[k] = 0.0f;
    } else {
        for (int k = 1; k <= (n - 1) / 2; k++)     ceps[k] *= 2.0f;
        for (int k = (n - 1) / 2 + 1; k < n; k++)  ceps[k] = 0.0f;
    }

    /* 5) X = FFT(ceps · window)  → complex cepstrum of the min-phase signal */
    fftwf_execute(p_fwd2);

    /* 6) X ← exp(X)   :  exp(a + jb) = e^a · (cos b + j sin b) */
    for (int k = 0; k < half; k++) {
        double a  = (double)X[k][0];
        double b  = (double)X[k][1];
        double ea = exp(a);
        X[k][0] = (float)(ea * cos(b));
        X[k][1] = (float)(ea * sin(b));
    }

    /* 7) out = IFFT(exp(...)).real  (÷ n) */
    fftwf_execute(p_inv2);
    for (int k = 0; k < n; k++) out[k] = (double)out_buf[k] * (double)inv_n;

    fftwf_destroy_plan(p_fwd1);
    fftwf_destroy_plan(p_inv1);
    fftwf_destroy_plan(p_fwd2);
    fftwf_destroy_plan(p_inv2);
    fftwf_free(X);
    fftwf_free(real_buf);
    fftwf_free(ceps);
    fftwf_free(out_buf);
    return 0;
}

int fft_convolve_truncate(const double *a, int len_a,
                          const double *b, int len_b,
                          double *out, int out_len) {
    int conv_len = len_a + len_b - 1;
    int N        = next_pow2(conv_len);
    int half     = N / 2 + 1;

    float        *ap     = fftwf_alloc_real((size_t)N);
    float        *bp     = fftwf_alloc_real((size_t)N);
    fftwf_complex *Af     = fftwf_alloc_complex((size_t)half);
    fftwf_complex *Bf     = fftwf_alloc_complex((size_t)half);
    float        *result = fftwf_alloc_real((size_t)N);
    if (!ap || !bp || !Af || !Bf || !result) {
        if (ap)     fftwf_free(ap);
        if (bp)     fftwf_free(bp);
        if (Af)     fftwf_free(Af);
        if (Bf)     fftwf_free(Bf);
        if (result) fftwf_free(result);
        return -2;
    }

    memset(ap, 0, (size_t)N * sizeof(float));
    memset(bp, 0, (size_t)N * sizeof(float));
    for (int i = 0; i < len_a; i++) ap[i] = (float)a[i];
    for (int i = 0; i < len_b; i++) bp[i] = (float)b[i];

    fftwf_plan pa = fftwf_plan_dft_r2c_1d(N, ap, Af,     FFTW_ESTIMATE);
    fftwf_plan pb = fftwf_plan_dft_r2c_1d(N, bp, Bf,     FFTW_ESTIMATE);
    fftwf_plan pi = fftwf_plan_dft_c2r_1d(N, Af, result, FFTW_ESTIMATE);

    fftwf_execute(pa);
    fftwf_execute(pb);

    /* Point-wise multiplication in the frequency domain,
     * with the 1/N IFFT normalisation folded into Af. */
    float inv_N = 1.0f / (float)N;
    for (int k = 0; k < half; k++) {
        float ar = Af[k][0], ai = Af[k][1];
        float br = Bf[k][0], bi = Bf[k][1];
        Af[k][0] = (ar * br - ai * bi) * inv_N;
        Af[k][1] = (ar * bi + ai * br) * inv_N;
    }

    fftwf_execute(pi);

    int copy_len = (out_len < N) ? out_len : N;
    for (int k = 0; k < copy_len; k++) out[k] = (double)result[k];
    for (int k = copy_len; k < out_len; k++) out[k] = 0.0;

    fftwf_destroy_plan(pa);
    fftwf_destroy_plan(pb);
    fftwf_destroy_plan(pi);
    fftwf_free(ap);
    fftwf_free(bp);
    fftwf_free(Af);
    fftwf_free(Bf);
    fftwf_free(result);
    return 0;
}
