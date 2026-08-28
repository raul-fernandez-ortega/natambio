/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 * Equivalence test between the symmetric and the asymmetric XTC designs.
 *
 * This is the cross-check that justifies keeping xtc.c and xtc_asym.c as two
 * independent implementations: process_asym() fed identical parameters on both
 * sides must reproduce process() to numerical tolerance. A structural error in
 * the asymmetric ladder (a wrong rung index, a misplaced ILD application, a
 * swapped side) shows up as an O(1) difference; what remains here is the
 * rounding of ~17 double-precision FFT convolutions, which lands around 1e-9
 * relative. The tolerance is set well above that and far below any real defect.
 *
 * The same cross-check is run a second time on the fractional-ITD path
 * (get_xtc_frac / get_xtc_asym_frac), plus two checks the integer path cannot
 * make: that each fractional recursion collapses onto its integer counterpart
 * when the ITD happens to be a whole number of samples, and that it does NOT
 * when it does not. Together they pin the frequency-domain evaluation to the
 * time-domain one wherever the two are supposed to agree.
 *
 * Run with `make check` in this directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "xtc.h"
#include "xtc_asym.h"

#define TOL_EQUIV   1e-7    /* symmetric vs asymmetric, relative to peak */
#define TOL_IDENT   1e-12   /* cross_left vs cross_right, must be identical */

/* Integer vs fractional recursion at a whole-sample ITD. Both evaluate the same
 * ladder, one by writing at array indices and one through a pair of transforms,
 * so what separates them is FFT rounding, not method: measured around 1e-16
 * relative. The tolerance sits well above that and far below any structural
 * difference. */
#define TOL_FRAC    1e-10

/* Below this, the fractional and integer designs would be telling us the ITD
 * rounding costs nothing -- which at 180 us / 48 kHz (8.64 samples, rounded to
 * 9) it demonstrably does. The check is here so that a get_xtc_frac() that
 * silently rounded its argument could not pass the suite. */
#define TOL_DIFFERS 1e-3

static double maxabs(const double *x, int n) {
    double m = 0.0;
    for (int i = 0; i < n; i++) if (fabs(x[i]) > m) m = fabs(x[i]);
    return m;
}

static double maxdiff(const double *a, const double *b, int n, int *at) {
    double m = 0.0;
    *at = -1;
    for (int i = 0; i < n; i++) {
        double d = fabs(a[i] - b[i]);
        if (d > m) { m = d; *at = i; }
    }
    return m;
}

static int compare(const char *what, const double *a, const double *b,
                   int n, double tol) {
    int at;
    double d   = maxdiff(a, b, n, &at);
    double ref = maxabs(a, n);
    double rel = (ref > 0.0) ? d / ref : d;
    int ok = (rel <= tol);
    printf("  %-26s maxdiff=%9.3e at n=%-5d rel=%9.3e  %s\n",
           what, d, at, rel, ok ? "OK" : "FAILED");
    return ok;
}

static int peak_index(const double *x, int n) {
    int p = 0;
    for (int i = 0; i < n; i++) if (fabs(x[i]) > fabs(x[p])) p = i;
    return p;
}

int main(void) {
    const int    L     = 4096;
    const int    sr    = 48000;
    const int    itd   = 180;
    const int    az    = 20;
    const double ild   = 10.0;
    const double alpha = 1.8;
    int ok = 1;

    double *d_sym   = calloc((size_t)L, sizeof(double));
    double *c_sym   = calloc((size_t)L, sizeof(double));
    double *d_asym  = calloc((size_t)L, sizeof(double));
    double *cl_asym = calloc((size_t)L, sizeof(double));
    double *cr_asym = calloc((size_t)L, sizeof(double));
    if (!d_sym || !c_sym || !d_asym || !cl_asym || !cr_asym) {
        fprintf(stderr, "allocation failed\n");
        return 77;   /* automake: skip rather than fail on OOM */
    }

    printf("== process(): symmetric reference ==\n");
    if (process(itd, ild, alpha, az, sr, L, 0, 0, d_sym, c_sym) != 0) {
        fprintf(stderr, "process() failed\n");
        return 1;
    }

    printf("\n== process_asym(): both sides identical ==\n");
    xtc_asym_side same = { .itd_us      = itd,
                           .ild_db      = ild,
                           .ild_alpha   = alpha,
                           .azimuth_deg = az };
    if (process_asym(&same, &same, sr, L, 0, 0, d_asym, cl_asym, cr_asym) != 0) {
        fprintf(stderr, "process_asym() failed\n");
        return 1;
    }

    printf("\n== Equivalence (relative tolerance %g) ==\n", TOL_EQUIV);
    ok &= compare("direct  asym vs sym",  d_sym, d_asym,  L, TOL_EQUIV);
    ok &= compare("cross_l asym vs sym",  c_sym, cl_asym, L, TOL_EQUIV);
    ok &= compare("cross_r asym vs sym",  c_sym, cr_asym, L, TOL_EQUIV);
    ok &= compare("cross_l vs cross_r",   cl_asym, cr_asym, L, TOL_IDENT);

    /* A genuinely asymmetric layout. Beyond staying finite, the geometry has a
     * signature worth asserting: each cross filter cancels the OTHER speaker's
     * crosstalk, so its leading tap sits at the other side's ITD. Getting the
     * two sides crossed is the likeliest wiring mistake, and it is exactly what
     * this catches. */
    const int itd_l = 180, itd_r = 140;
    xtc_asym_side left  = { .itd_us = itd_l, .ild_db = 10.0,
                            .ild_alpha = 1.8, .azimuth_deg = 20 };
    xtc_asym_side right = { .itd_us = itd_r, .ild_db =  8.0,
                            .ild_alpha = 1.9, .azimuth_deg = 15 };

    printf("\n== Asymmetric layout (L: 180 us/10 dB/20 deg, R: 140 us/8 dB/15 deg) ==\n");
    if (process_asym(&left, &right, sr, L, 0, 0, d_asym, cl_asym, cr_asym) != 0) {
        fprintf(stderr, "process_asym() failed on the asymmetric case\n");
        return 1;
    }

    for (int i = 0; i < L; i++) {
        if (!isfinite(d_asym[i]) || !isfinite(cl_asym[i]) || !isfinite(cr_asym[i])) {
            printf("  non-finite tap at n=%d                          FAILED\n", i);
            ok = 0;
            break;
        }
    }

    const int s_l = (int)round(itd_l * 1e-6 * sr);
    const int s_r = (int)round(itd_r * 1e-6 * sr);
    const int p_l = peak_index(cl_asym, L);
    const int p_r = peak_index(cr_asym, L);

    printf("  cross_l peak at n=%-4d (ITD right = %d)             %s\n",
           p_l, s_r, p_l == s_r ? "OK" : "FAILED");
    printf("  cross_r peak at n=%-4d (ITD left  = %d)             %s\n",
           p_r, s_l, p_r == s_l ? "OK" : "FAILED");
    ok &= (p_l == s_r) && (p_r == s_l);

    int at;
    double sep = maxdiff(cl_asym, cr_asym, L, &at);
    printf("  cross_l differs from cross_r: %.4e                %s\n",
           sep, sep > 0.0 ? "OK" : "FAILED");
    ok &= (sep > 0.0);

    printf("  direct[0] = %.6f                                 %s\n",
           d_asym[0], d_asym[0] == 1.0 ? "OK" : "FAILED");
    ok &= (d_asym[0] == 1.0);


    /* ---------------------------------------------------------------------
     * Fractional ITD. Same cross-check, plus the two the integer path cannot
     * make: that the frequency-domain recursion collapses onto the
     * time-domain one at a whole-sample ITD, and that it does not at a
     * fractional one.
     * ------------------------------------------------------------------ */
    double *d_symf  = calloc((size_t)L, sizeof(double));
    double *c_symf  = calloc((size_t)L, sizeof(double));
    double *d_asymf = calloc((size_t)L, sizeof(double));
    double *cl_asymf = calloc((size_t)L, sizeof(double));
    double *cr_asymf = calloc((size_t)L, sizeof(double));
    if (!d_symf || !c_symf || !d_asymf || !cl_asymf || !cr_asymf) {
        fprintf(stderr, "allocation failed\n");
        return 77;
    }

    /* 180 us at 48 kHz is 8.64 samples: the integer path rounds it to 9, the
     * fractional one does not, which is what the last two checks turn on. */
    printf("\n== Fractional ITD: %d us at %d Hz = %.4f samples ==\n",
           itd, sr, itd * 1e-6 * sr);

    if (process(itd, ild, alpha, az, sr, L, 1, XTC_DEFAULT_MODEL_DELAY,
                d_symf, c_symf) != 0) {
        fprintf(stderr, "process() failed on the fractional path\n");
        return 1;
    }
    if (process_asym(&same, &same, sr, L, 1, XTC_DEFAULT_MODEL_DELAY,
                     d_asymf, cl_asymf, cr_asymf) != 0) {
        fprintf(stderr, "process_asym() failed on the fractional path\n");
        return 1;
    }
    ok &= compare("direct  asym vs sym",  d_symf, d_asymf,  L, TOL_EQUIV);
    ok &= compare("cross_l asym vs sym",  c_symf, cl_asymf, L, TOL_EQUIV);
    ok &= compare("cross_r asym vs sym",  c_symf, cr_asymf, L, TOL_EQUIV);
    ok &= compare("cross_l vs cross_r",   cl_asymf, cr_asymf, L, TOL_IDENT);

    /* It must actually differ from the rounded design, or the fractional path
     * is quietly rounding after all. Model delay 0 so the two are compared at
     * the same origin and only the sub-sample shift separates them. */
    printf("\n== Fractional vs integer, same parameters ==\n");
    if (process(itd, ild, alpha, az, sr, L, 1, 0, d_symf, c_symf) != 0) {
        fprintf(stderr, "process() failed on the fractional path\n");
        return 1;
    }
    double moved = maxdiff(c_symf, c_sym, L, &at) / maxabs(c_sym, L);
    printf("  %-26s reldiff=%9.3e                     %s\n",
           "cross frac vs integer", moved,
           moved > TOL_DIFFERS ? "OK" : "FAILED");
    ok &= (moved > TOL_DIFFERS);

    /* And it must NOT differ when there is nothing to gain: 250 us at 48 kHz is
     * exactly 12 samples, so the two recursions are evaluating the same ladder
     * and only FFT rounding may separate them. */
    const int itd_exact_us = 250;
    printf("\n== Whole-sample ITD (%d us = %.4f samples): the two paths must agree ==\n",
           itd_exact_us, itd_exact_us * 1e-6 * sr);
    if (process(itd_exact_us, ild, alpha, az, sr, L, 0, 0, d_sym,  c_sym)  != 0 ||
        process(itd_exact_us, ild, alpha, az, sr, L, 1, 0, d_symf, c_symf) != 0) {
        fprintf(stderr, "process() failed at the whole-sample ITD\n");
        return 1;
    }
    ok &= compare("direct frac vs integer", d_sym, d_symf, L, TOL_FRAC);
    ok &= compare("cross  frac vs integer", c_sym, c_symf, L, TOL_FRAC);

    free(d_symf); free(c_symf); free(d_asymf); free(cl_asymf); free(cr_asymf);

    free(d_sym); free(c_sym); free(d_asym); free(cl_asym); free(cr_asym);

    printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
