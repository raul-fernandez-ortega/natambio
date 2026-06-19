#ifndef LOUDNESS_H
#define LOUDNESS_H

/* Equal-loudness (isophonic) contour models, ported from
 * ~/curvas_isofonicas/generate_isophonic.py. Used to synthesise a loudness
 * compensation FIR: the *difference* between the contour at a target phon level
 * and the contour at a reference phon level, each normalised to 0 dB at 1 kHz.
 *
 * Supported model names (matching the Python CLI):
 *   "iso226-2003", "iso226-2023"  : analytic ISO 226 formula
 *   "fletcher-munson"             : tabulated Fletcher-Munson 1933 curves
 *   "a-weighting","b-weighting","c-weighting"
 *                                 : IEC weighting curves (ignore phon, so the
 *                                   difference curve is flat at 0 dB)
 * "robinson-dadson" is a stub in the reference and is not supported here.
 */

/* Build the equal-loudness *difference* curve (contour at `phon` minus contour
 * at `ref_phon`) for `model`, on the model's native frequency grid, each
 * contour normalised to 0 dB at 1 kHz. Allocates *out_freq and *out_db (the
 * caller must free() both) and sets *out_n. Returns 0 on success, non-zero on
 * error (unknown/unsupported model or allocation failure). */
int loudness_diff_curve(const char *model, double phon, double ref_phon,
                        double **out_freq, double **out_db, int *out_n);

/* Context for loudness_db_model: a (freq, dB) curve, ascending in frequency. */
typedef struct {
    const double *freq;   /* model frequencies, Hz, ascending */
    const double *db;     /* curve value in dB at each frequency */
    int           n;      /* number of points */
} loudness_model_ctx;

/* firwin2_db_model_fn-compatible callback (see dsp.h): returns the curve dB at
 * f_hz by log-frequency interpolation, log-linear extrapolation beyond the
 * tabulated range, and a -200 dB sentinel outside [10 Hz, 20 kHz]. `ctx` must
 * point to a loudness_model_ctx. */
double loudness_db_model(double f_hz, void *ctx);

#endif
