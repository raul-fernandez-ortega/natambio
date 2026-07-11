/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "xtc.h"
#include "dsp.h"
#include "binaural_cues.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Implemented in main.c. Declared here so process() can emit debug WAVs
 * without a dedicated WAV I/O header. */
int write_wav(const char *path, const double *data, int n, int sample_rate);

#define DB_OCT             6.0
#define FLIM               200.0
#define HF_SHELF_HZ        20000.0   /* HF shelf corner frequency */
#define HF_SHELF_DB_OCT    -36.0     /* shelf slope above HF_SHELF_HZ */
#define NSTEPS             32

/* Input limits. These exist to make overflow unrepresentable rather than to
 * express taste: MAX_FILTER_LEN keeps 2*len-1 and its next-power-of-2 padding
 * inside int range inside dsp.c, and MAX_SAMPLE_RATE bounds itd_us * rate. */
#define XTC_MAX_FILTER_LEN  (1 << 20)   /* 1048576 taps (~21 s @ 48 kHz) */
#define XTC_MAX_SAMPLE_RATE 768000
#define XTC_MAX_ILD_ALPHA   100.0

/* Context and callback for firwin2_db_model_fn.
 *   - HF (f > FLIM):  -ild_log_empirical(theta, f, alpha)
 *   - LF (f <= FLIM): -6 dB/oct anchored to the model value at FLIM
 *   - HF shelf: HF_SHELF_DB_OCT/oct above HF_SHELF_HZ, to smooth the
 *     step at Nyquist and reduce cepstral ringing at n > filter_len/2 of the MP.
 * firwin2 guarantees f_hz > 0, so log2(f_hz) is safe. */
typedef struct {
    double theta;
    double alpha;
} ild_model_ctx;

static double ild_db_model(double f_hz, void *ctx_v) {
    const ild_model_ctx *c = (const ild_model_ctx *)ctx_v;
    double db;
    if (f_hz > FLIM) {
        db = -ild_log_empirical(c->theta, f_hz, c->alpha);
    } else {
        double ild_flim = ild_log_empirical(c->theta, FLIM, c->alpha);
        db = -DB_OCT * (log2(FLIM) - log2(f_hz)) - ild_flim;
    }
    if (f_hz > HF_SHELF_HZ) {
        db += HF_SHELF_DB_OCT * log2(f_hz / HF_SHELF_HZ);
    }
    return db;
}

static double rms(const double *x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += x[i] * x[i];
    return sqrt(s / (double)n);
}

static double l2norm(const double *x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += x[i] * x[i];
    return sqrt(s);
}

#ifdef DEBUG
/* Saves a peak-normalised copy without touching the original buffer.
 * Used for ILD_*.wav and MP_ILD_*.wav, whose in-memory contents are still
 * live in the downstream pipeline (RMS / L2 / XTC normalisation) and must
 * not be mutated. */
static int write_wav_peak_normalized(const char *path, const double *data,
                                     int n, int sample_rate) {
    double peak = 0.0;
    for (int i = 0; i < n; i++) {
        double a = fabs(data[i]);
        if (a > peak) peak = a;
    }
    if (peak <= 0.0) return write_wav(path, data, n, sample_rate);

    double *scratch = malloc((size_t)n * sizeof(double));
    if (!scratch) {
        fprintf(stderr, "Memory allocation failed for %s\n", path);
        return -1;
    }
    double inv = 1.0 / peak;
    for (int i = 0; i < n; i++) scratch[i] = data[i] * inv;
    int rc = write_wav(path, scratch, n, sample_rate);
    free(scratch);
    return rc;
}
#endif /* DEBUG */

int get_xtc(int length, double attenuation, int delay,
             const double *ild_filter,
             double *direct_out, double *cross_out) {
    if (!ild_filter || !direct_out || !cross_out) return -1;
    if (length < 2 || length > XTC_MAX_FILTER_LEN)  return -1;
    if (delay <= 0)                                 return -1;

    /* attenuation must be a positive dB step. This is not a style check: it is
     * what keeps the ladder below convergent. With attenuation > 0, dir_att and
     * cross_att stay negative for every iteration, so both pow() calls are
     * bounded by 1. A zero or negative value turns the per-step attenuations
     * into gains and pow() runs away to +inf, poisoning both filters. */
    if (!isfinite(attenuation) || attenuation <= 0.0) return -1;

    /* The ladder starts at (NSTEPS-1)*delay and walks down by 2*delay. Bound
     * delay so neither the start index nor the step can overflow int. Indices
     * are still carried in long long below, so the bounds tests themselves are
     * well defined even at the limit. */
    if (delay > INT_MAX / (2 * NSTEPS)) return -1;

    /* A single non-finite tap in the ILD filter would spread across the whole
     * impulse response on the first convolution. */
    for (int i = 0; i < length; i++) {
        if (!isfinite(ild_filter[i])) return -1;
    }

    memset(direct_out, 0, (size_t)length * sizeof(double));
    memset(cross_out,  0, (size_t)length * sizeof(double));

    long long n         = (long long)(NSTEPS - 1) * (long long)delay;
    double    cross_att = -attenuation * (double)(NSTEPS - 1);
    double    dir_att   = -attenuation * (double)NSTEPS;

    double *tmp_d = malloc((size_t)length * sizeof(double));
    double *tmp_c = malloc((size_t)length * sizeof(double));
    if (!tmp_d || !tmp_c) {
        free(tmp_d);
        free(tmp_c);
        return -2;
    }

    while (n > 0) {
        /* Taps landing past the end of the filter are dropped rather than
         * written: they fall outside the representable impulse response, which
         * is the same truncation fft_convolve_truncate applies to everything
         * else. process() warns when this costs a significant part of the
         * ladder. */
        const long long dir_idx = n + (long long)delay;
        if (dir_idx < (long long)length) {
            direct_out[dir_idx] += pow(10.0, dir_att / 20.0);
        }
        if (n < (long long)length) {
            cross_out[n] += -pow(10.0, cross_att / 20.0);
        }

        if (fft_convolve_truncate(direct_out, length, ild_filter, length, tmp_d, length) != 0 ||
            fft_convolve_truncate(cross_out,  length, ild_filter, length, tmp_c, length) != 0) {
            free(tmp_d);
            free(tmp_c);
            return -3;
        }

        memcpy(direct_out, tmp_d, (size_t)length * sizeof(double));
        memcpy(cross_out,  tmp_c, (size_t)length * sizeof(double));

        dir_att   += 2.0 * attenuation;
        cross_att += 2.0 * attenuation;
        n         -= 2LL * (long long)delay;
    }

    direct_out[0] = 1.0;

    free(tmp_d);
    free(tmp_c);
    return 0;
}

int process(int itd_us, double ild_db, double ild_alpha,
            int azimuth_deg, int sample_rate, int filter_len,
            double *direct_out, double *cross_out) {
    /* Everything reaching this function comes straight from the XML, so it is
     * validated here rather than trusted. */
    if (!direct_out || !cross_out) {
        fprintf(stderr, "xtc: null output buffer\n");
        return -1;
    }
    if (filter_len < 2 || filter_len > XTC_MAX_FILTER_LEN) {
        fprintf(stderr, "xtc: filter_len %d outside [2, %d]\n",
                filter_len, XTC_MAX_FILTER_LEN);
        return -1;
    }
    if (sample_rate <= 0 || sample_rate > XTC_MAX_SAMPLE_RATE) {
        fprintf(stderr, "xtc: sample_rate %d outside (0, %d]\n",
                sample_rate, XTC_MAX_SAMPLE_RATE);
        return -1;
    }
    if (itd_us <= 0) {
        fprintf(stderr, "xtc: itd_us must be positive (got %d)\n", itd_us);
        return -1;
    }
    if (!isfinite(ild_db) || ild_db <= 0.0) {
        fprintf(stderr, "xtc: ild_db must be a positive dB step (got %g)\n", ild_db);
        return -1;
    }
    if (!isfinite(ild_alpha) || fabs(ild_alpha) > XTC_MAX_ILD_ALPHA) {
        fprintf(stderr, "xtc: ild_alpha %g outside [-%g, %g]\n",
                ild_alpha, XTC_MAX_ILD_ALPHA, XTC_MAX_ILD_ALPHA);
        return -1;
    }

    /* itd_us * sample_rate overflows int well before either operand does, so
     * the product is formed in double and range-checked before narrowing. */
    const double itd_exact = (double)itd_us * 1e-6 * (double)sample_rate;
    if (!(itd_exact >= 0.5) || itd_exact > (double)(INT_MAX / (2 * NSTEPS))) {
        fprintf(stderr, "xtc: ITD %d us at %d Hz gives %.3f samples, outside [1, %d]\n",
                itd_us, sample_rate, itd_exact, INT_MAX / (2 * NSTEPS));
        return -1;
    }
    int itd_samples = (int)round(itd_exact);

    /* The ladder's first tap sits at (NSTEPS-1)*itd_samples. If that is already
     * past the end of the filter, get_xtc drops those taps and the cancellation
     * comes out incomplete: the filter is simply too short for this ITD. Design
     * still proceeds -- the result is a valid, safe, weaker filter -- but the
     * user gets told, because silently returning a filter that does not cancel
     * is worse than a noisy one that does. */
    const long long first_tap = (long long)(NSTEPS - 1) * (long long)itd_samples;
    if (first_tap >= (long long)filter_len) {
        fprintf(stderr,
                "xtc: warning: filter_len %d is too short for an ITD of %d samples; "
                "taps at or beyond index %lld are dropped and crosstalk cancellation "
                "will be incomplete. Use filter_len > %lld.\n",
                filter_len, itd_samples, first_tap, first_tap);
    }

    ild_model_ctx model_ctx = {
        .theta = deg2rad((double)azimuth_deg),
        .alpha = ild_alpha,
    };

    double *h_linear = calloc((size_t)filter_len, sizeof(double));
    double *h_min    = calloc((size_t)filter_len, sizeof(double));
    if (!h_linear || !h_min) {
        fprintf(stderr, "Memory allocation failed\n");
        free(h_linear); free(h_min);
        return -1;
    }

    int rc = firwin2(filter_len, sample_rate, ild_db_model, &model_ctx, h_linear);
    if (rc != 0) {
        fprintf(stderr, "firwin2 failed (rc=%d)\n", rc);
        free(h_linear); free(h_min);
        return -2;
    }

    /* RMS normalisation of the linear-phase filter */
    double rms_h = rms(h_linear, filter_len);
    if (rms_h > 0.0) {
        for (int i = 0; i < filter_len; i++) h_linear[i] /= rms_h;
    }

    rc = minimum_phase(h_linear, filter_len, h_min);
    if (rc != 0) {
        fprintf(stderr, "minimum_phase failed (rc=%d)\n", rc);
        free(h_linear); free(h_min);
        return -3;
    }

    /* RMS normalisation of the minimum-phase filter */
    double rms_m = rms(h_min, filter_len);
    if (rms_m > 0.0) {
        for (int i = 0; i < filter_len; i++) h_min[i] /= rms_m;
    }

#ifdef DEBUG
    /* Intermediate filters for inspection (only if DEBUG is defined at compile time) */
    char dbg_path[512];
    snprintf(dbg_path, sizeof(dbg_path), "filters/ILD_%d_deg.wav", azimuth_deg);
    write_wav_peak_normalized(dbg_path, h_linear, filter_len, sample_rate);
    snprintf(dbg_path, sizeof(dbg_path), "filters/MP_ILD_%d_deg.wav", azimuth_deg);
    write_wav_peak_normalized(dbg_path, h_min,    filter_len, sample_rate);
#endif

    /* L2 normalisation before entering the XTC recursion */
    double l2 = l2norm(h_min, filter_len);
    if (l2 > 0.0) {
        for (int i = 0; i < filter_len; i++) h_min[i] /= l2;
    }

    printf("Calculating XTC filters for: delay --> %d samples. Attenuation --> %.2f dB. "
           "azimuth --> %d degrees\n", itd_samples, ild_db, azimuth_deg);
    printf("XTC filters length: %d samples. Sample rate: %d\n", filter_len, sample_rate);

    rc = get_xtc(filter_len, ild_db, itd_samples, h_min, direct_out, cross_out);
    if (rc != 0) {
        fprintf(stderr, "get_xtc failed (rc=%d)\n", rc);
        free(h_linear); free(h_min);
        return -4;
    }

    free(h_linear);
    free(h_min);
    return 0;
}
