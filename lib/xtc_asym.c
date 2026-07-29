/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "xtc_asym.h"
#include "dsp.h"
#include "binaural_cues.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI ((double) 3.14159265358979323846264338327950288)
#endif

/* These mirror the constants of xtc.c on purpose. xtc.c is not included nor
 * modified here because third-party ports of NatAmbio mirror that file and it
 * must stay byte-stable; the price is this duplication. Both copies must be
 * kept in step if the ILD model ever changes. */
#define DB_OCT             6.0
#define FLIM               200.0
#define HF_SHELF_HZ        20000.0   /* HF shelf corner frequency */
#define HF_SHELF_DB_OCT    -36.0     /* shelf slope above HF_SHELF_HZ */
#define NSTEPS             32

/* Rungs of the ladder. xtc.c walks NSTEPS in half-steps, which is NSTEPS/2
 * round trips; the asymmetric recursion is written directly in round trips, so
 * the two produce the same number of terms. */
#define NRUNGS             (NSTEPS / 2)

#define XTC_MAX_FILTER_LEN  (1 << 20)   /* 1048576 taps (~21 s @ 48 kHz) */
#define XTC_MAX_SAMPLE_RATE 768000
#define XTC_MAX_ILD_ALPHA   100.0

/* Context and callback for firwin2_db_model_fn; identical in shape to the one
 * in xtc.c. Note that ild_log_empirical() is linear in alpha*sin(theta), so
 * passing theta = pi/2 turns `alpha` into that product directly. That is how
 * the round-trip (mean) filter below is built without a second model. */
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

/* build_ild_filter — one G of the model: design the ILD magnitude with firwin2,
 * convert to minimum phase and normalise. Same sequence, same normalisations
 * and same order as process() in xtc.c, so that identical parameters yield an
 * identical filter.
 *
 * theta is in radians; alpha is the model scale factor (or, when theta = pi/2,
 * the alpha*sin(theta) product directly). Returns 0 on success. */
static int build_ild_filter(double theta, double alpha, int sample_rate,
                            int filter_len, double *out)
{
    ild_model_ctx ctx = { .theta = theta, .alpha = alpha };

    double *h_linear = calloc((size_t)filter_len, sizeof(double));
    if (!h_linear) {
        fprintf(stderr, "xtc_asym: memory allocation failed\n");
        return -1;
    }

    int rc = firwin2(filter_len, sample_rate, ild_db_model, &ctx, h_linear);
    if (rc != 0) {
        fprintf(stderr, "xtc_asym: firwin2 failed (rc=%d)\n", rc);
        free(h_linear);
        return -2;
    }

    double rms_h = rms(h_linear, filter_len);
    if (rms_h > 0.0) {
        for (int i = 0; i < filter_len; i++) h_linear[i] /= rms_h;
    }

    rc = minimum_phase(h_linear, filter_len, out);
    free(h_linear);
    if (rc != 0) {
        fprintf(stderr, "xtc_asym: minimum_phase failed (rc=%d)\n", rc);
        return -3;
    }

    double rms_m = rms(out, filter_len);
    if (rms_m > 0.0) {
        for (int i = 0; i < filter_len; i++) out[i] /= rms_m;
    }

    /* L2 normalisation before entering the recursion: the filter must carry
     * spectral shape only, never level. The level lives in the per-rung dB
     * terms of get_xtc_asym(). */
    double l2 = l2norm(out, filter_len);
    if (l2 > 0.0) {
        for (int i = 0; i < filter_len; i++) out[i] /= l2;
    }
    return 0;
}

int get_xtc_asym(int length,
                 double att_l, double att_r,
                 int delay_l, int delay_r,
                 const double *ild_mean,
                 const double *ild_l, const double *ild_r,
                 double *direct_out,
                 double *cross_left_out, double *cross_right_out)
{
    if (!ild_mean || !ild_l || !ild_r)                     return -1;
    if (!direct_out || !cross_left_out || !cross_right_out) return -1;
    if (length < 2 || length > XTC_MAX_FILTER_LEN)          return -1;
    if (delay_l <= 0 || delay_r <= 0)                       return -1;

    /* Both attenuations must be positive dB steps. This is the convergence
     * condition of the asymmetric model, |G_l * G_r| < 1: with att_l, att_r > 0
     * every rung is strictly below the previous one and the pow() calls stay
     * bounded by 1. A zero or negative value turns them into gains and the
     * ladder runs away to +inf. Note that the condition is on the *product*, so
     * the two sides need not be equally attenuated. */
    if (!isfinite(att_l) || att_l <= 0.0) return -1;
    if (!isfinite(att_r) || att_r <= 0.0) return -1;

    /* Highest index touched is NRUNGS*(delay_l+delay_r). Bound each delay so
     * neither that product nor the intermediate sums can overflow int; indices
     * are carried in long long below so the tests themselves stay well defined. */
    if (delay_l > INT_MAX / (2 * NSTEPS)) return -1;
    if (delay_r > INT_MAX / (2 * NSTEPS)) return -1;

    /* A single non-finite tap in any ILD filter would spread across the whole
     * impulse response on the first convolution. */
    for (int i = 0; i < length; i++) {
        if (!isfinite(ild_mean[i]) || !isfinite(ild_l[i]) || !isfinite(ild_r[i]))
            return -1;
    }

    memset(direct_out,      0, (size_t)length * sizeof(double));
    memset(cross_left_out,  0, (size_t)length * sizeof(double));
    memset(cross_right_out, 0, (size_t)length * sizeof(double));

    const long long T        = (long long)delay_l + (long long)delay_r;
    const double    att_step = att_l + att_r;

    double *tmp_d = malloc((size_t)length * sizeof(double));
    double *tmp_l = malloc((size_t)length * sizeof(double));
    double *tmp_r = malloc((size_t)length * sizeof(double));
    if (!tmp_d || !tmp_l || !tmp_r) {
        free(tmp_d); free(tmp_l); free(tmp_r);
        return -2;
    }

    /* The ladder is built from the top rung down, exactly as in get_xtc(): a
     * tap added at iteration k is convolved once per remaining iteration, so it
     * ends up carrying k applications of the round-trip ILD filter. That is the
     * convention of the symmetric implementation -- the ILD *spectrum* is
     * applied once per round trip while the delay and the broadband ILD follow
     * the full (2i-1)/(2i) law -- and it is reproduced here so that equal
     * parameters on both sides give the same filters as get_xtc().
     *
     * Rung i of the direct filter sits at i*T with (a_l*a_r)^i, and rung i of
     * the cross filter feeding the left speaker sits at (i-1)*T + delay_r with
     * a_r*(a_l*a_r)^(i-1) -- it cancels the RIGHT speaker's crosstalk, hence
     * the right side's delay, level and (below) spectrum. */
    for (int k = NRUNGS; k >= 1; k--) {
        /* Taps landing past the end of the filter are dropped rather than
         * written; same truncation semantics fft_convolve_truncate applies to
         * everything else. process_asym() warns when this costs a significant
         * part of the ladder. */
        const long long dir_idx = (long long)k * T;
        if (dir_idx < (long long)length)
            direct_out[dir_idx] += pow(10.0, -(double)k * att_step / 20.0);

        /* Cross rung i = k+1: added one iteration after the direct rung of the
         * same index so that it accumulates i-1 round-trip convolutions, the
         * remaining one being the per-side filter applied after the loop. */
        if (k < NRUNGS) {
            const long long l_idx = (long long)k * T + (long long)delay_r;
            const long long r_idx = (long long)k * T + (long long)delay_l;
            if (l_idx < (long long)length)
                cross_left_out[l_idx]  += -pow(10.0, -(att_r + (double)k * att_step) / 20.0);
            if (r_idx < (long long)length)
                cross_right_out[r_idx] += -pow(10.0, -(att_l + (double)k * att_step) / 20.0);
        }

        if (fft_convolve_truncate(direct_out, length, ild_mean, length, tmp_d, length) != 0) {
            free(tmp_d); free(tmp_l); free(tmp_r);
            return -3;
        }
        memcpy(direct_out, tmp_d, (size_t)length * sizeof(double));

        /* Skipped on the first pass, where both cross accumulators are still
         * all zeros and the convolution would be pure work. */
        if (k < NRUNGS) {
            if (fft_convolve_truncate(cross_left_out,  length, ild_mean, length, tmp_l, length) != 0 ||
                fft_convolve_truncate(cross_right_out, length, ild_mean, length, tmp_r, length) != 0) {
                free(tmp_d); free(tmp_l); free(tmp_r);
                return -3;
            }
            memcpy(cross_left_out,  tmp_l, (size_t)length * sizeof(double));
            memcpy(cross_right_out, tmp_r, (size_t)length * sizeof(double));
        }
    }

    /* Rung i = 1 of each cross filter: the first cancellation, which has not
     * made any round trip yet and therefore carries no round-trip filter. */
    if ((long long)delay_r < (long long)length)
        cross_left_out[delay_r]  += -pow(10.0, -att_r / 20.0);
    if ((long long)delay_l < (long long)length)
        cross_right_out[delay_l] += -pow(10.0, -att_l / 20.0);

    /* The single per-side application: every cross rung crosses its own side's
     * head shadow exactly once, whatever its order. This is what keeps the two
     * cross filters spectrally distinct in an asymmetric layout. */
    if (fft_convolve_truncate(cross_left_out,  length, ild_r, length, tmp_l, length) != 0 ||
        fft_convolve_truncate(cross_right_out, length, ild_l, length, tmp_r, length) != 0) {
        free(tmp_d); free(tmp_l); free(tmp_r);
        return -3;
    }
    memcpy(cross_left_out,  tmp_l, (size_t)length * sizeof(double));
    memcpy(cross_right_out, tmp_r, (size_t)length * sizeof(double));

    /* The leading delta: the signal of each channel is delivered intact and the
     * ladder is added on top. Index 0 is untouched by the loop -- every tap
     * sits at a strictly positive index and the minimum-phase filters are
     * causal -- so this is an assignment of an otherwise empty sample. */
    direct_out[0] = 1.0;

    free(tmp_d); free(tmp_l); free(tmp_r);
    return 0;
}

/* Validates one side and converts its ITD to samples. Returns the ITD in
 * samples, or -1 after reporting the offending parameter. */
static int check_side(const xtc_asym_side *s, const char *tag, int sample_rate)
{
    if (s->itd_us <= 0) {
        fprintf(stderr, "xtc_asym: <%s> itd_us must be positive (got %d)\n", tag, s->itd_us);
        return -1;
    }
    if (!isfinite(s->ild_db) || s->ild_db <= 0.0) {
        fprintf(stderr, "xtc_asym: <%s> ild_db must be a positive dB step (got %g)\n",
                tag, s->ild_db);
        return -1;
    }
    if (!isfinite(s->ild_alpha) || fabs(s->ild_alpha) > XTC_MAX_ILD_ALPHA) {
        fprintf(stderr, "xtc_asym: <%s> ild_alpha %g outside [-%g, %g]\n",
                tag, s->ild_alpha, XTC_MAX_ILD_ALPHA, XTC_MAX_ILD_ALPHA);
        return -1;
    }

    /* itd_us * sample_rate overflows int well before either operand does, so
     * the product is formed in double and range-checked before narrowing. */
    const double itd_exact = (double)s->itd_us * 1e-6 * (double)sample_rate;
    if (!(itd_exact >= 0.5) || itd_exact > (double)(INT_MAX / (2 * NSTEPS))) {
        fprintf(stderr, "xtc_asym: <%s> ITD %d us at %d Hz gives %.3f samples, outside [1, %d]\n",
                tag, s->itd_us, sample_rate, itd_exact, INT_MAX / (2 * NSTEPS));
        return -1;
    }
    return (int)round(itd_exact);
}

int process_asym(const xtc_asym_side *left, const xtc_asym_side *right,
                 int sample_rate, int filter_len,
                 double *direct_out,
                 double *cross_left_out, double *cross_right_out)
{
    if (!left || !right) {
        fprintf(stderr, "xtc_asym: null side parameters\n");
        return -1;
    }
    if (!direct_out || !cross_left_out || !cross_right_out) {
        fprintf(stderr, "xtc_asym: null output buffer\n");
        return -1;
    }
    if (filter_len < 2 || filter_len > XTC_MAX_FILTER_LEN) {
        fprintf(stderr, "xtc_asym: filter_len %d outside [2, %d]\n",
                filter_len, XTC_MAX_FILTER_LEN);
        return -1;
    }
    if (sample_rate <= 0 || sample_rate > XTC_MAX_SAMPLE_RATE) {
        fprintf(stderr, "xtc_asym: sample_rate %d outside (0, %d]\n",
                sample_rate, XTC_MAX_SAMPLE_RATE);
        return -1;
    }

    const int itd_l = check_side(left,  "left",  sample_rate);
    const int itd_r = check_side(right, "right", sample_rate);
    if (itd_l < 0 || itd_r < 0) return -1;

    /* The ladder's top rung sits at NRUNGS*(itd_l+itd_r). If that is already
     * past the end of the filter those taps are dropped and the cancellation
     * comes out incomplete: the filter is simply too short for this geometry.
     * Design still proceeds -- the result is a valid, weaker filter -- but the
     * user gets told. Unlike the symmetric case, what bounds the ladder is the
     * SUM of both ITDs, so a layout can stay within budget by keeping the mean
     * ITD even when the two sides differ widely. */
    const long long top_tap = (long long)NRUNGS * ((long long)itd_l + (long long)itd_r);
    if (top_tap >= (long long)filter_len) {
        fprintf(stderr,
                "xtc_asym: warning: filter_len %d is too short for ITDs of %d + %d samples; "
                "taps at or beyond index %lld are dropped and crosstalk cancellation "
                "will be incomplete. Use filter_len > %lld.\n",
                filter_len, itd_l, itd_r, top_tap, top_tap);
    }

    const double theta_l = deg2rad((double)left->azimuth_deg);
    const double theta_r = deg2rad((double)right->azimuth_deg);

    /* Round-trip filter. Applying the ILD spectrum once per round trip (the
     * convention of get_xtc()) leaves room for a single filter per rung, and a
     * round trip crosses one head shadow on each side; its log-magnitude is
     * therefore the mean of both sides'. Since the model is linear in
     * alpha*sin(theta), that mean is exactly the model evaluated at theta = pi/2
     * with alpha = mean of the two alpha*sin(theta) products -- no averaging of
     * responses needed. With both sides equal it collapses to the side filter,
     * which is what makes the symmetric case reduce to get_xtc(). */
    const double kappa_mean = 0.5 * (left->ild_alpha  * sin(theta_l) +
                                     right->ild_alpha * sin(theta_r));

    double *ild_mean = calloc((size_t)filter_len, sizeof(double));
    double *ild_l    = calloc((size_t)filter_len, sizeof(double));
    double *ild_r    = calloc((size_t)filter_len, sizeof(double));
    if (!ild_mean || !ild_l || !ild_r) {
        fprintf(stderr, "xtc_asym: memory allocation failed\n");
        free(ild_mean); free(ild_l); free(ild_r);
        return -1;
    }

    int rc = build_ild_filter(theta_l, left->ild_alpha, sample_rate, filter_len, ild_l);
    if (rc == 0)
        rc = build_ild_filter(theta_r, right->ild_alpha, sample_rate, filter_len, ild_r);
    if (rc == 0)
        rc = build_ild_filter(M_PI / 2.0, kappa_mean, sample_rate, filter_len, ild_mean);
    if (rc != 0) {
        free(ild_mean); free(ild_l); free(ild_r);
        return -2;
    }

    printf("Calculating asymmetric XTC filters:\n");
    printf("\tleft : delay --> %d samples. Attenuation --> %.2f dB. azimuth --> %d degrees. alpha --> %.2f\n",
           itd_l, left->ild_db, left->azimuth_deg, left->ild_alpha);
    printf("\tright: delay --> %d samples. Attenuation --> %.2f dB. azimuth --> %d degrees. alpha --> %.2f\n",
           itd_r, right->ild_db, right->azimuth_deg, right->ild_alpha);
    printf("XTC filters length: %d samples. Sample rate: %d\n", filter_len, sample_rate);

    rc = get_xtc_asym(filter_len, left->ild_db, right->ild_db, itd_l, itd_r,
                      ild_mean, ild_l, ild_r,
                      direct_out, cross_left_out, cross_right_out);

    free(ild_mean); free(ild_l); free(ild_r);

    if (rc != 0) {
        fprintf(stderr, "xtc_asym: get_xtc_asym failed (rc=%d)\n", rc);
        return -4;
    }
    return 0;
}
