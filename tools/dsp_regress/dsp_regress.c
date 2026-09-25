/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

/* dsp_regress -- one filter, dumped raw, so that two builds of lib/ can be
 * compared with cmp.
 *
 * It is the probe half of regress.sh, which builds it twice -- once against a
 * reference lib/ taken out of git, once against the working tree's -- and
 * diffs the results case by case. Everything here therefore uses ONLY the
 * long-standing lib/ interface: a probe that called something new would fail
 * to compile against the reference and there would be nothing to compare.
 * Adding a case for a new function is the wrong instinct; the point of this is
 * what did NOT change.
 *
 *     dsp_regress <what> <out.bin> [args...]
 *
 * The output is raw doubles in host order, which is the format cmp wants. No
 * WAV: libsndfile stamps a float file with the time of day and two identical
 * runs would differ.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dsp.h"
#include "loudness.h"
#include "xtc.h"
#include "xtc_asym.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int dump(const char *path, const double *a, int na,
                const double *b, int nb, const double *c, int nc)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) { fprintf(stderr, "dsp_regress: cannot write %s\n", path); return 1; }
    if (a) fwrite(a, sizeof(double), (size_t) na, f);
    if (b) fwrite(b, sizeof(double), (size_t) nb, f);
    if (c) fwrite(c, sizeof(double), (size_t) nc, f);
    fclose(f);
    return 0;
}

/* Two magnitude models for the bare firwin2 case, defined HERE and not taken
   from anywhere in lib/, so that what is being compared is firwin2 and not
   some model that moved underneath it. */
typedef struct { double fc, slope; } slope_ctx;

static double model_slope(double f_hz, void *v)      /* flat, then a straight line */
{
    const slope_ctx *c = (const slope_ctx *) v;
    if (f_hz <= c->fc) return 0.0;
    return -c->slope * log2(f_hz / c->fc);
}

static double model_tilt(double f_hz, void *v)       /* a tilt across the band */
{
    (void) v;
    return -3.0 * log2(f_hz / 1000.0);
}

static void usage(void)
{
    fprintf(stderr,
      "usage: dsp_regress <what> <out.bin> [args]\n"
      "  firwin2   <out> <taps> <rate> <fc> <slope>   flat-then-slope model\n"
      "  tilt      <out> <taps> <rate>                -3 dB/octave from 1 kHz\n"
      "  minphase  <out> <taps> <rate> <fc> <slope>   firwin2 then minimum_phase\n"
      "  loudness  <out> <taps> <rate> <model> <phon> <ref_phon>\n"
      "  xtc       <out> <taps> <rate> <itd_us> <ild_db> <alpha> <az>\n"
      "  xtcasym   <out> <taps> <rate> <itd_l> <ild_l> <al_l> <az_l>\n"
      "                                 <itd_r> <ild_r> <al_r> <az_r>\n"
      "  convolve  <out> <na> <nb>                    fft_convolve_truncate\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(); return 2; }
    const char *what = argv[1];
    const char *out  = argv[2];

    if (strcmp(what, "firwin2") == 0 || strcmp(what, "minphase") == 0) {
        if (argc < 7) { usage(); return 2; }
        int n = atoi(argv[3]), rate = atoi(argv[4]);
        slope_ctx ctx; ctx.fc = atof(argv[5]); ctx.slope = atof(argv[6]);
        double *h = malloc((size_t) n * sizeof(double));
        double *m = malloc((size_t) n * sizeof(double));
        if (!h || !m) return 3;
        if (firwin2(n, rate, model_slope, &ctx, h)) { fprintf(stderr, "firwin2 failed\n"); return 1; }
        if (strcmp(what, "minphase") == 0) {
            if (minimum_phase(h, n, m)) { fprintf(stderr, "minimum_phase failed\n"); return 1; }
            return dump(out, m, n, h, n, NULL, 0);
        }
        return dump(out, h, n, NULL, 0, NULL, 0);
    }

    if (strcmp(what, "tilt") == 0) {
        if (argc < 5) { usage(); return 2; }
        int n = atoi(argv[3]), rate = atoi(argv[4]);
        double *h = malloc((size_t) n * sizeof(double));
        if (!h) return 3;
        if (firwin2(n, rate, model_tilt, NULL, h)) { fprintf(stderr, "firwin2 failed\n"); return 1; }
        return dump(out, h, n, NULL, 0, NULL, 0);
    }

    if (strcmp(what, "loudness") == 0) {
        if (argc < 8) { usage(); return 2; }
        int n = atoi(argv[3]), rate = atoi(argv[4]);
        const char *model = argv[5];
        double phon = atof(argv[6]), ref = atof(argv[7]);
        double *cf = NULL, *cd = NULL; int cn = 0;
        if (loudness_diff_curve(model, phon, ref, &cf, &cd, &cn)) {
            fprintf(stderr, "loudness_diff_curve failed for '%s'\n", model); return 1;
        }
        double *h = malloc((size_t) n * sizeof(double));
        double *m = malloc((size_t) n * sizeof(double));
        if (!h || !m) return 3;
        loudness_model_ctx ctx; ctx.freq = cf; ctx.db = cd; ctx.n = cn;
        if (firwin2(n, rate, loudness_db_model, &ctx, h)) { fprintf(stderr, "firwin2 failed\n"); return 1; }
        if (minimum_phase(h, n, m)) { fprintf(stderr, "minimum_phase failed\n"); return 1; }
        free(cf); free(cd);
        /* Both: the coeff that gets applied and the linear-phase design behind
           it, so a difference can be placed on one side or the other. */
        return dump(out, m, n, h, n, NULL, 0);
    }

    if (strcmp(what, "xtc") == 0) {
        if (argc < 9) { usage(); return 2; }
        int n = atoi(argv[3]), rate = atoi(argv[4]);
        double *d = malloc((size_t) n * sizeof(double));
        double *c = malloc((size_t) n * sizeof(double));
        if (!d || !c) return 3;
        if (process(atoi(argv[5]), atof(argv[6]), atof(argv[7]), atoi(argv[8]),
                    rate, n, 1, xtc_model_delay(rate), d, c)) {
            fprintf(stderr, "process failed\n"); return 1;
        }
        return dump(out, d, n, c, n, NULL, 0);
    }

    if (strcmp(what, "xtcasym") == 0) {
        if (argc < 13) { usage(); return 2; }
        int n = atoi(argv[3]), rate = atoi(argv[4]);
        xtc_asym_side l, r;
        l.itd_us = atoi(argv[5]);  l.ild_db = atof(argv[6]);
        l.ild_alpha = atof(argv[7]); l.azimuth_deg = atoi(argv[8]);
        r.itd_us = atoi(argv[9]);  r.ild_db = atof(argv[10]);
        r.ild_alpha = atof(argv[11]); r.azimuth_deg = atoi(argv[12]);
        double *d  = malloc((size_t) n * sizeof(double));
        double *cl = malloc((size_t) n * sizeof(double));
        double *cr = malloc((size_t) n * sizeof(double));
        if (!d || !cl || !cr) return 3;
        if (process_asym(&l, &r, rate, n, 1, xtc_model_delay(rate), d, cl, cr)) {
            fprintf(stderr, "process_asym failed\n"); return 1;
        }
        return dump(out, d, n, cl, n, cr, n);
    }

    if (strcmp(what, "convolve") == 0) {
        if (argc < 5) { usage(); return 2; }
        int na = atoi(argv[3]), nb = atoi(argv[4]);
        int no = na + nb - 1;
        double *a = malloc((size_t) na * sizeof(double));
        double *b = malloc((size_t) nb * sizeof(double));
        double *o = malloc((size_t) no * sizeof(double));
        if (!a || !b || !o) return 3;
        /* Deterministic and not random: two runs of this have to agree. */
        for (int i = 0; i < na; i++) a[i] = sin(0.001 * i) * exp(-0.0001 * i);
        for (int i = 0; i < nb; i++) b[i] = cos(0.0013 * i) / (1.0 + 0.001 * i);
        if (fft_convolve_truncate(a, na, b, nb, o, no)) {
            fprintf(stderr, "fft_convolve_truncate failed\n"); return 1;
        }
        return dump(out, o, no, NULL, 0, NULL, 0);
    }

    usage();
    return 2;
}
