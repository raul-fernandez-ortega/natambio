#include "xtc.h"
#include "dsp.h"
#include "binaural_cues.h"

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
    if (length < 2 || delay <= 0) return -1;

    memset(direct_out, 0, (size_t)length * sizeof(double));
    memset(cross_out,  0, (size_t)length * sizeof(double));

    int    n         = (NSTEPS - 1) * delay;
    double cross_att = -attenuation * (double)(NSTEPS - 1);
    double dir_att   = -attenuation * (double)NSTEPS;

    double *tmp_d = malloc((size_t)length * sizeof(double));
    double *tmp_c = malloc((size_t)length * sizeof(double));
    if (!tmp_d || !tmp_c) {
        free(tmp_d);
        free(tmp_c);
        return -2;
    }

    while (n > 0) {
        if (n + delay < length) {
            direct_out[n + delay] += pow(10.0, dir_att / 20.0);
        }
        cross_out[n] += -pow(10.0, cross_att / 20.0);

        fft_convolve_truncate(direct_out, length, ild_filter, length, tmp_d, length);
        fft_convolve_truncate(cross_out,  length, ild_filter, length, tmp_c, length);

        memcpy(direct_out, tmp_d, (size_t)length * sizeof(double));
        memcpy(cross_out,  tmp_c, (size_t)length * sizeof(double));

        dir_att   += 2.0 * attenuation;
        cross_att += 2.0 * attenuation;
        n         -= 2 * delay;
    }

    direct_out[0] = 1.0;

    free(tmp_d);
    free(tmp_c);
    return 0;
}

int process(int itd_us, double ild_db, double ild_alpha,
            int azimuth_deg, int sample_rate, int filter_len,
            double *direct_out, double *cross_out) {
    int itd_samples = (int)round((double)itd_us * 1e-6 * (double)sample_rate);

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
