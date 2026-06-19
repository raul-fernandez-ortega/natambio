#ifndef XTC_H
#define XTC_H

/* getXTC — port of the XTC recursion from ambio_filters_scipy.py.
 *
 *   length      : length of the direct/cross filters
 *   attenuation : ILD per step, in positive dB
 *   delay       : ITD in samples
 *   ild_filter  : L2-normalised min-phase ILD filter impulse (length samples)
 *   direct_out  : direct output (length samples, allocated by the caller)
 *   cross_out   : cross output  (length samples, allocated by the caller)
 *
 * 32 iterations alternating direct/cross, each convolved with ild_filter
 * and truncated to length.
 */
int get_xtc(int length, double attenuation, int delay,
             const double *ild_filter,
             double *direct_out, double *cross_out);

/* process — ILD → minimum-phase → XTC pipeline.
 *   itd_us, ild_db, ild_alpha, azimuth_deg, sample_rate, filter_len : filter parameters
 *   direct_out, cross_out : filter_len-double buffers allocated by the caller;
 *                           process fills them but does not persist them to disk.
 * If DEBUG is defined at compile time, writes peak-normalised filters/ILD_*.wav
 * and filters/MP_ILD_*.wav as a verification side-product.
 * Returns 0 on success, non-zero on error.
 */
int process(int itd_us, double ild_db, double ild_alpha,
            int azimuth_deg, int sample_rate, int filter_len,
            double *direct_out, double *cross_out);

#endif
