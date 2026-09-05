/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _NAE_ERB_HPP_
#define _NAE_ERB_HPP_

extern "C" {

#include <fftw3.h>

}

#include "nae.hpp"

/* NAE with one PCA per ERB-like band instead of one over the whole spectrum.
 *
 * Everything but the decomposition is NAE's: the thread, the semaphore, the
 * buffers, the three gains and their slew, the width matrix, the correlation
 * that drives beta, the timer. This class replaces decompose() and
 * advanceBlock() and nothing else.
 *
 * THE BANK NEVER TOUCHES THE AUDIO. Splitting the signal into twenty bands,
 * running a PCA on each and adding the results back is not what happens here,
 * because it does not have to. Two identities make the bank disappear:
 *
 *   the covariance of band b over the window is the window's own spectrum
 *   weighted by W_b(f)^2 -- Parseval -- so every axis comes from ONE transform
 *   with no inverse and no delay;
 *
 *   each band reconstructs through a constant 2x2 projector P_b = v_b v_b^T
 *   applied to W_b(f) X(f), so the sum over bands is
 *
 *       C1(f) = [ sum_b W_b(f) P1_b ] X(f)  =  G1(f) X(f)
 *
 *   -- one frequency-dependent 2x2 matrix, whatever the number of bands, and
 *   G1 + G2 = I because sum_b W_b = 1 and P1_b + P2_b = I.
 *
 * So the band count never enters the transform count. Per period: two forward
 * transforms, a weighted sum and a 2x2 eigenproblem per band, six vector
 * combinations, four inverse transforms. Around 1% of a 256-frame period at
 * 48 kHz, whether the bank has ten bands or forty.
 *
 * THREE WINDOWS, all ending at the sample that has just arrived.
 *
 *   The reconstruction window, covsteps * sample_count, is NAE's and is not
 *   touched: the overlap-add and the latency are exactly what they were.
 *
 *   The analysis window, <cov_window_ms>, is longer and reaches further into
 *   the PAST. It costs no latency -- those samples have been played already --
 *   and it is what makes the axis steady. Measured off line on "I Am In Love",
 *   the median frame-to-frame movement of the axis falls from 9.65 degrees at
 *   16 ms to 3.65 at 64 ms, which is the broadband engine's own 3.34.
 *
 *   The synthesis transform is the same length as the analysis one, which is
 *   the headroom the circular convolution needs. Applied over exactly the
 *   reconstruction window there is none, and the tail of the window wraps onto
 *   its head -- which is the frame about to be emitted. That wrap cancels
 *   between C1 and C2, since G1 + G2 = I, so it is invisible to any check on
 *   their sum and audible in either alone: off line it sat 7.9 dB ABOVE C1
 *   between 10 and 20 kHz before the headroom was there, and 60 dB below it
 *   after. It was heard before it was measured.
 *
 * The bank itself is ERB-like and floored: B_b = max(ERB(f_b), band_min_hz).
 * A window of N samples cannot resolve finer than fs/N however the bank is
 * built, so below the frequency where an ERB is narrower than the floor the
 * bands stop narrowing and the centres spread to match. The floor is given in
 * Hz rather than as a multiple of fs/N because it is a physical width and does
 * not move when the window does.
 */

/* What the configuration may say. Everything else about the bank is fixed
   below, deliberately: the shape, the order, the covered range and the centre
   spacing rule were swept off line and none of them earned a knob. */
#define NA_ERB_COV_WINDOW_MS   64.0    /* <cov_window_ms> */
#define NA_ERB_DELTA_ERB        2.0    /* <delta_erb> */
#define NA_ERB_BAND_MIN_HZ    125.0    /* <band_min_hz> */

/* What it may not. The width of each filter in ERB units, the gammatone order,
   the covered range, and the floor on centre spacing as a multiple of the width
   floor. */
#define NA_ERB_BANDWIDTH_ERB    1.0
#define NA_ERB_ORDER            4
#define NA_ERB_FMIN            20.0
#define NA_ERB_FMAX_NYQUIST     0.95
#define NA_ERB_ETA              1.0
/* Patterson/Holdsworth: what makes a 4th-order gammatone's equivalent
   rectangular bandwidth come out at ERB(f_c) rather than 2 % under it. */
#define NA_ERB_GAMMATONE_FACTOR 1.019
/* A ceiling on the bank, so a configuration cannot ask for an unbounded
   allocation on the audio path. Twenty bands is the tested figure; forty is
   already more resolution than a 64 ms window supports. */
#define NA_ERB_MAX_BANDS      128

class NaeErb : public NAE {

private:

  /* From the configuration. */
  double cov_window_ms;
  double delta_erb;
  double band_min_hz;

  /* The bank, once the rate and the period are known. n_cov is the analysis
     and synthesis window, always a whole number of periods and never shorter
     than the reconstruction window; syn_offset is where in it the frame being
     emitted sits, which is covsteps-1 frames from the end. */
  int n_cov;
  int n_bins;
  int n_bands;
  int n_pca;
  int syn_offset;

  double *centers;      /* n_bands */
  double *bandwidths;   /* n_bands */
  double *masks;        /* n_bands * n_bins, W_b(f), a partition of unity */
  double *masks2;       /* n_bands * n_bins, W_b(f)^2, the covariance weights */
  double *fold;         /* n_bins, 2 except DC (0, which removes the mean) and
                           Nyquist (1): a one-sided spectrum summed as a full
                           one */

  /* The history the analysis looks back over, mid and side, oldest first. The
     side already carries the side_weight of the period each sample entered
     with, as nae.cpp writes it -- older frames are never rescaled. */
  double *mid_hist;
  double *side_hist;

  /* Scratch, all of it allocated once. */
  fftw_plan plan_fwd_mid;
  fftw_plan plan_fwd_side;
  fftw_plan plan_inv[4];
  fftw_complex *spec_mid;
  fftw_complex *spec_side;
  fftw_complex *prod[4];        /* C1 mid, C1 side, C2 mid, C2 side */
  double *out_time[4];
  double *p_mm, *p_ss, *p_ms;   /* n_bins, the folded spectral products */
  double *r_mm, *r_ss, *r_ms;   /* n_bands, the 2x2 covariances */
  double *gcoef[6];             /* n_bins: G1 mm/ms/ss then G2 mm/ms/ss */

  /* The last covsteps sets of projector coefficients, three per band per
     component, and where the next one goes. Summing the filters of the last
     covsteps windows and applying once is identical to applying each and
     summing -- which is what the overlap-add does -- and it is what lets the
     synthesis happen at the moment of emission, when the frame has context on
     both sides. Only the per-band coefficients are kept, not the per-bin
     arrays: sum_k masks^T p_k = masks^T sum_k p_k. */
  double *ring1;                /* covsteps * 3 * n_bands */
  double *ring2;
  int ring_pos;

  bool erb_ready;

  void buildBank(void);
  void freeEngine(void);

public:

  NaeErb(string n_name, int n_mode);
  /* Stops and joins the worker BEFORE anything of this class is freed. The
     worker calls decompose(), a virtual of THIS class, so letting ~NAE() do the
     stopping would leave it running against half-destroyed state. */
  ~NaeErb(void);

  void setCovWindowMs(double ms);
  void setDeltaErb(double d);
  void setBandMinHz(double hz);
  double getCovWindowMs(void) { return cov_window_ms; };
  double getDeltaErb(void) { return delta_erb; };
  double getBandMinHz(void) { return band_min_hz; };
  /* For the report: how many bands the configuration produced, and over how
     long a window. Known only after load(). */
  int getBandCount(void) { return n_bands; };
  int getCovWindowSamples(void) { return n_cov; };

  /* Builds the bank and the transforms, then hands over to NAE::load() for the
     buffers and the thread. Everything FFTW does that is not fftw_execute()
     happens here, and never on the audio path. */
  void load(int abspri, int policy);

protected:

  void decompose(void);
  void advanceBlock(void);

};

#endif
