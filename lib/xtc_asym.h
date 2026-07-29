/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef XTC_ASYM_H
#define XTC_ASYM_H

/* Asymmetric XTC: crosstalk cancellation for layouts where the two speakers do
 * not sit at the same azimuth from the listener. See
 * docs/xtc/xtc_no_simetrico_es.md for the model.
 *
 * This unit is deliberately self-contained and does NOT touch xtc.c/xtc.h: those
 * are mirrored by third-party ports of NatAmbio and must stay byte-stable. The
 * ILD-model constants and the design pipeline below are therefore duplicated
 * from xtc.c rather than shared; any change to the model must be applied to both.
 *
 * Relation to the symmetric case: with left and right parameters identical, the
 * filters produced here match get_xtc()/process() to numerical tolerance, with
 * cross_left == cross_right. That equivalence is what test_xtc_asym.c checks.
 */

/* Per-speaker parameters. One of these describes one side's acoustic path,
 * i.e. exactly one G of the model. */
typedef struct {
    int    itd_us;       /* inter-aural time difference, microseconds (> 0)    */
    double ild_db;       /* inter-aural level difference per step, dB (> 0)    */
    double ild_alpha;    /* log-empirical ILD model scale factor              */
    int    azimuth_deg;  /* source azimuth, degrees                           */
} xtc_asym_side;

/* get_xtc_asym — asymmetric XTC recursion. Produces M^-1, i.e. the filters
 * WITHOUT the balance factor b: the level trim between channels is applied
 * downstream as a routing gain (see docs/xtc/xtc_no_simetrico_es.md and the
 * <xtc_asym> section of src/README.md).
 *
 *   length        : length of every output filter
 *   att_l, att_r  : ILD per step of each side, positive dB
 *   delay_l,
 *   delay_r       : ITD of each side, in samples (> 0)
 *   ild_mean      : L2-normalised min-phase ILD filter of the round trip
 *                   (built from the mean of both sides' log-magnitudes)
 *   ild_l, ild_r  : L2-normalised min-phase ILD filter of each side
 *   direct_out    : direct filter, shared by both channels (length samples)
 *   cross_left_out  : cross filter feeding the LEFT speaker  (length samples)
 *   cross_right_out : cross filter feeding the RIGHT speaker (length samples)
 *
 * All output buffers are allocated by the caller and fully overwritten.
 * Returns 0 on success, non-zero on error.
 */
int get_xtc_asym(int length,
                 double att_l, double att_r,
                 int delay_l, int delay_r,
                 const double *ild_mean,
                 const double *ild_l, const double *ild_r,
                 double *direct_out,
                 double *cross_left_out, double *cross_right_out);

/* process_asym — ILD -> minimum-phase -> asymmetric XTC pipeline.
 *   left, right : per-side parameters
 *   sample_rate, filter_len : as in process() (xtc.c)
 *   direct_out, cross_left_out, cross_right_out : filter_len-double buffers
 *                                                 allocated by the caller.
 * Returns 0 on success, non-zero on error.
 */
int process_asym(const xtc_asym_side *left, const xtc_asym_side *right,
                 int sample_rate, int filter_len,
                 double *direct_out,
                 double *cross_left_out, double *cross_right_out);

#endif
