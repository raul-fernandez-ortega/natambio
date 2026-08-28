/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef XTC_H
#define XTC_H

/* Bulk delay the fractional-ITD path adds to both filters, in samples, so that
 * the two-sided impulse response of a fractional shift is not clipped at n = 0.
 * 64 taps leave the discarded pre-ring at -52 dB of the cross filter, about
 * 20 dB below the recursion's own residual; the cost is 1.3 ms of latency at
 * 48 kHz, applied equally to direct and cross and therefore inaudible as
 * anything but latency. */
#define XTC_DEFAULT_MODEL_DELAY 64

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

/* get_xtc_frac — the same recursion with an ITD that need not be a whole
 * number of samples.
 *
 *   delay       : ITD in samples, fractional (> 0)
 *   model_delay : bulk delay in samples added to BOTH outputs (>= 0)
 *   everything else as in get_xtc()
 *
 * get_xtc() places a tap by writing at an array index, so the ITD has to be
 * rounded first. That rounding is not free: an ITD error of dt leaves a
 * residual 2*sin(pi*f*dt) relative to the cancelling signal, so at 48 kHz the
 * default 170 us (8.16 samples, rounded to 8) caps crosstalk suppression at
 * about -27 dB above 8 kHz, where the exact ITD reaches the truncation floor
 * of the ladder some 55 dB lower.
 *
 * Here the recursion is evaluated in the frequency domain instead, iteration
 * for iteration: same taps, same gains, same Horner ordering, but a tap is a
 * linear-phase factor exp(-j*2*pi*f*delay) rather than an index. That is the
 * exact band-limited delay operator, so there is no interpolation kernel and no
 * accuracy/length trade-off. With an integer `delay` it reproduces get_xtc() to
 * numerical precision.
 *
 * A fractional shift has a two-sided impulse response, so the first cross tap
 * carries energy at negative time; model_delay shifts both filters forward so
 * that energy is kept rather than clipped. XTC depends only on the delay
 * BETWEEN the two filters, so a delay common to both changes nothing but
 * latency. XTC_DEFAULT_MODEL_DELAY is the recommended value.
 *
 * Returns 0 on success, non-zero on error.
 */
int get_xtc_frac(int length, double attenuation, double delay,
                 const double *ild_filter, int model_delay,
                 double *direct_out, double *cross_out);

/* process — ILD → minimum-phase → XTC pipeline.
 *   itd_us, ild_db, ild_alpha, azimuth_deg, sample_rate, filter_len : filter parameters
 *   frac_delay  : non-zero to run the recursion at the exact, unrounded ITD
 *                 (get_xtc_frac); zero to round it to whole samples (get_xtc)
 *   model_delay : bulk delay for the fractional path, ignored when frac_delay
 *                 is zero. Pass XTC_DEFAULT_MODEL_DELAY unless you have a
 *                 reason not to.
 *   direct_out, cross_out : filter_len-double buffers allocated by the caller;
 *                           process fills them but does not persist them to disk.
 * If DEBUG is defined at compile time, writes peak-normalised filters/ILD_*.wav
 * and filters/MP_ILD_*.wav as a verification side-product.
 * Returns 0 on success, non-zero on error.
 */
int process(int itd_us, double ild_db, double ild_alpha,
            int azimuth_deg, int sample_rate, int filter_len,
            int frac_delay, int model_delay,
            double *direct_out, double *cross_out);

#endif
