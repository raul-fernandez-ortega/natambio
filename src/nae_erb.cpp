/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#include "nae_erb.hpp"

#if defined(__SSE2__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

/* ---------------------------------------------------------------------------
 * The ERB scale, and the bank built on it
 * ------------------------------------------------------------------------- */

static double hz_to_erb(double f)
{
  return 21.4 * log10(1.0 + 4.37 * f / 1000.0);
}

static double erb_to_hz(double e)
{
  return (1000.0 / 4.37) * (pow(10.0, e / 21.4) - 1.0);
}

/* The equivalent rectangular bandwidth at f. */
static double erb_width(double f)
{
  return 24.7 * (4.37 * f / 1000.0 + 1.0);
}

/* |H(f)| of an n-th order gammatone centred at fc: the closed form of the
   magnitude of a t^(n-1) exp(-2 pi B t) cos(2 pi fc t). Only the magnitude is
   used -- the phase every band ends up with is the common one the partition of
   unity gives it, which is none at all. */
static double gammatone_mag(double f, double fc, double bw, int order)
{
  double x = (f - fc) / bw;
  return pow(1.0 + x * x, -0.5 * (double)order);
}

NaeErb::NaeErb(string n_name, int n_mode) : NAE(n_name, n_mode)
{
  cov_window_ms = NA_ERB_COV_WINDOW_MS;
  delta_erb = NA_ERB_DELTA_ERB;
  band_min_hz = NA_ERB_BAND_MIN_HZ;

  n_cov = n_bins = n_bands = n_pca = syn_offset = 0;
  centers = bandwidths = masks = masks2 = fold = NULL;
  mid_hist = side_hist = NULL;
  plan_fwd_mid = plan_fwd_side = NULL;
  spec_mid = spec_side = NULL;
  p_mm = p_ss = p_ms = NULL;
  r_mm = r_ss = r_ms = NULL;
  ring1 = ring2 = sum1 = sum2 = NULL;
  ring_pos = 0;
  erb_ready = false;
  for(int i = 0; i < 4; i++) {
    plan_inv[i] = NULL;
    prod[i] = NULL;
    out_time[i] = NULL;
  }
  for(int i = 0; i < 6; i++)
    gcoef[i] = NULL;
}

NaeErb::~NaeErb(void)
{
  /* The worker is inside decompose(), which is ours. Stopping it here rather
     than letting ~NAE() do it is the difference between a clean exit and a
     thread reading a freed bank: by the time the base destructor runs, every
     member of this class is already gone. */
  run = false;
  sem_post(&semaphore);
  if(t_proc != 0) {
    pthread_join(t_proc, NULL);
    t_proc = 0;
  }
  freeEngine();
}

void NaeErb::freeEngine(void)
{
  if(plan_fwd_mid) fftw_destroy_plan(plan_fwd_mid);
  if(plan_fwd_side) fftw_destroy_plan(plan_fwd_side);
  for(int i = 0; i < 4; i++) {
    if(plan_inv[i]) fftw_destroy_plan(plan_inv[i]);
    if(prod[i]) fftw_free(prod[i]);
    if(out_time[i]) fftw_free(out_time[i]);
  }
  if(spec_mid) fftw_free(spec_mid);
  if(spec_side) fftw_free(spec_side);
  if(mid_hist) fftw_free(mid_hist);
  if(side_hist) fftw_free(side_hist);
  free(centers);
  free(bandwidths);
  free(masks);
  free(masks2);
  free(fold);
  free(p_mm); free(p_ss); free(p_ms);
  free(r_mm); free(r_ss); free(r_ms);
  for(int i = 0; i < 6; i++)
    free(gcoef[i]);
  free(ring1);
  free(ring2);
  free(sum1);
  free(sum2);
}

void NaeErb::setCovWindowMs(double ms)
{
  if(ms > 0.0)
    cov_window_ms = ms;
}

void NaeErb::setDeltaErb(double d)
{
  if(d > 0.0)
    delta_erb = d;
}

void NaeErb::setBandMinHz(double hz)
{
  if(hz > 0.0)
    band_min_hz = hz;
}

/* The bank: centres, widths, masks. Called once, from load().
 *
 * The centres walk up from NA_ERB_FMIN, each one delta_erb away on the ERB-rate
 * scale or eta * band_min_hz away in Hz, whichever step is larger. Above the
 * frequency where an ERB is wider than the floor the first term wins and the
 * spacing is the ERB one; below it the second wins and the centres spread out
 * rather than crowding into a region the window cannot resolve anyway. Centres
 * closer together than the bands are wide would be a row of near-duplicates
 * with a PCA each and no more information between them than one would carry.
 *
 * The masks are then normalised point by point,
 *
 *     W_b(f) = A_b(f) / sum_j A_j(f)
 *
 * which is what makes them sum to one at every bin and the bank
 * perfect-reconstruction by construction. Normalising each filter to unit
 * energy would not: sum_b |H_b|^2 = 1 does not imply sum_b H_b = 1, and it is
 * the second that reconstruction needs. The price is that the outermost bands
 * stop being gammatone-shaped -- below the lowest centre and above the highest
 * there is nothing to share a bin with, so they take all of it -- which is why
 * the covered range is put outside the region under study.
 */
void NaeErb::buildBank(void)
{
  double nyquist = 0.5 * (double)sample_rate;
  double fmax = NA_ERB_FMAX_NYQUIST * nyquist;
  double step_floor = NA_ERB_ETA * band_min_hz;

  /* Count first, allocate once: this runs before the thread exists, but the
     arrays it sizes are read on the audio path and must not be grown there. */
  int count = 0;
  double f = NA_ERB_FMIN;
  while(f <= fmax && count < NA_ERB_MAX_BANDS) {
    count++;
    double erb_step = erb_to_hz(hz_to_erb(f) + delta_erb) - f;
    f += (erb_step > step_floor) ? erb_step : step_floor;
  }
  if(count < 1)
    count = 1;
  n_bands = count;

  centers = (double*) calloc(n_bands, sizeof(double));
  bandwidths = (double*) calloc(n_bands, sizeof(double));
  masks = (double*) calloc((size_t)n_bands * n_bins, sizeof(double));
  masks2 = (double*) calloc((size_t)n_bands * n_bins, sizeof(double));

  f = NA_ERB_FMIN;
  for(int b = 0; b < n_bands; b++) {
    centers[b] = f;
    double erb_term = NA_ERB_BANDWIDTH_ERB * NA_ERB_GAMMATONE_FACTOR * erb_width(f);
    bandwidths[b] = (erb_term > band_min_hz) ? erb_term : band_min_hz;
    double erb_step = erb_to_hz(hz_to_erb(f) + delta_erb) - f;
    f += (erb_step > step_floor) ? erb_step : step_floor;
  }

  /* The shapes, then the pointwise normalisation. */
  double df = (double)sample_rate / (double)n_cov;
  for(int k = 0; k < n_bins; k++) {
    double freq = df * (double)k;
    double sum = 0.0;
    for(int b = 0; b < n_bands; b++) {
      double a = gammatone_mag(freq, centers[b], bandwidths[b], NA_ERB_ORDER);
      masks[(size_t)k * n_bands + b] = a;
      sum += a;
    }
    /* sum is never zero: a gammatone magnitude decays as a power of the
       frequency distance and never reaches it. The guard is for the arithmetic,
       not for the physics. */
    if(sum <= 0.0)
      sum = 1.0;
    for(int b = 0; b < n_bands; b++) {
      double w = masks[(size_t)k * n_bands + b] / sum;
      masks[(size_t)k * n_bands + b] = w;
      masks2[(size_t)k * n_bands + b] = w * w;
    }
  }

  /* Turning a one-sided spectrum into the full Parseval sum: every bin twice
     except DC and, for an even length, Nyquist. DC is given zero rather than
     one, which removes the mean -- the covariance is about the variation, and
     the broadband engine subtracts it too. The synthesis below uses the
     untouched spectrum, DC included: zeroing it there would be a high-pass
     filter and not a statistic. */
  fold = (double*) calloc(n_bins, sizeof(double));
  for(int k = 0; k < n_bins; k++)
    fold[k] = 2.0;
  fold[0] = 0.0;
  if((n_cov % 2) == 0)
    fold[n_bins - 1] = 1.0;
}

void NaeErb::load(int abspri, int policy)
{
  n_pca = covsteps * sample_count;

  /* The analysis window: as many whole periods as <cov_window_ms> asks for, and
     never shorter than the reconstruction window, which it contains. */
  int want = (int)ceil(cov_window_ms * (double)sample_rate / 1000.0);
  int periods = (want + sample_count - 1) / sample_count;
  n_cov = periods * sample_count;
  if(n_cov < n_pca)
    n_cov = n_pca;
  n_bins = n_cov / 2 + 1;
  /* Where the frame being emitted sits in the window: covsteps-1 frames from
     the end, which is exactly where NAE's overlap-add emits from. It has
     (covsteps-1)*sample_count samples of context to its right and n_cov - n_pca
     to its left, and that context is what keeps the circular product from
     folding onto it. */
  syn_offset = n_cov - n_pca;

  buildBank();

  mid_hist = (double*) fftw_malloc(sizeof(double) * n_cov);
  side_hist = (double*) fftw_malloc(sizeof(double) * n_cov);
  memset(mid_hist, 0, sizeof(double) * n_cov);
  memset(side_hist, 0, sizeof(double) * n_cov);

  spec_mid = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n_bins);
  spec_side = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n_bins);
  for(int i = 0; i < 4; i++) {
    prod[i] = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n_bins);
    out_time[i] = (double*) fftw_malloc(sizeof(double) * n_cov);
  }

  p_mm = (double*) calloc(n_bins, sizeof(double));
  p_ss = (double*) calloc(n_bins, sizeof(double));
  p_ms = (double*) calloc(n_bins, sizeof(double));
  r_mm = (double*) calloc(n_bands, sizeof(double));
  r_ss = (double*) calloc(n_bands, sizeof(double));
  r_ms = (double*) calloc(n_bands, sizeof(double));
  for(int i = 0; i < 6; i++)
    gcoef[i] = (double*) calloc(n_bins, sizeof(double));

  ring1 = (double*) calloc((size_t)covsteps * 3 * n_bands, sizeof(double));
  ring2 = (double*) calloc((size_t)covsteps * 3 * n_bands, sizeof(double));
  sum1 = (double*) calloc((size_t)3 * n_bands, sizeof(double));
  sum2 = (double*) calloc((size_t)3 * n_bands, sizeof(double));
  ring_pos = 0;

  /* FFTW_MEASURE would time several algorithms and pick the fastest, which
     costs seconds here and buys a few microseconds a period. FFTW_ESTIMATE
     picks one from a model and, unlike MEASURE, does not scribble on the
     buffers while planning. Either way this is the only place FFTW is called
     from outside fftw_execute(): planning allocates, is not thread safe against
     itself, and has no business on the audio path. */
  plan_fwd_mid = fftw_plan_dft_r2c_1d(n_cov, mid_hist, spec_mid, FFTW_ESTIMATE);
  plan_fwd_side = fftw_plan_dft_r2c_1d(n_cov, side_hist, spec_side, FFTW_ESTIMATE);
  for(int i = 0; i < 4; i++)
    plan_inv[i] = fftw_plan_dft_c2r_1d(n_cov, prod[i], out_time[i], FFTW_ESTIMATE);

  erb_ready = true;

  if(!quiet) {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "NAE_ERB: " << name << ": " << n_bands << " bands, "
              << centers[0] << " Hz .. " << centers[n_bands - 1] << " Hz, "
              << "floor " << band_min_hz << " Hz, delta " << delta_erb << " ERB"
              << std::endl;
    std::cout << "NAE_ERB: " << name << ": analysis window " << n_cov
              << " samples (" << (1000.0 * n_cov / (double)sample_rate)
              << " ms, " << (n_cov / sample_count) << " periods), "
              << "reconstruction " << n_pca << " samples, latency unchanged"
              << std::endl;
  }

  NAE::load(abspri, policy);
}

/* ---------------------------------------------------------------------------
 * The block
 * ------------------------------------------------------------------------- */

void NaeErb::decompose(void)
{
#if defined(__SSE2__)
  /* Denormals off, once per block and costing two instructions. The band masks
     decay as the fourth power of the frequency distance, so the spectral
     products underflow into denormals in anything quiet, and a denormal on this
     path is not a rounding question but a stall of a hundred cycles. What it
     costs in exactness is values below 1e-308. */
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

  if(!erb_ready)
    return;

  int last = n_cov - sample_count;

  /* The period that has just arrived, at the end of the history. The width is
     applied first, walking from where prepareBlock() left it, and side_weight
     is written into this frame alone -- older frames keep the weight they were
     written with, which is what nae.cpp does and what the off-line reference
     was made to mirror. */
  double pa = pan_a_begin, pb = pan_b_begin;
  for(int i = 0; i < sample_count; i++, pa += pan_a_step, pb += pan_b_step) {
    double l = pa * left_in[i] + pb * right_in[i];
    double r = pb * left_in[i] + pa * right_in[i];
    mid_hist[last + i] = l + r;
    side_hist[last + i] = side_weight * (l - r);
  }

  fftw_execute(plan_fwd_mid);
  fftw_execute(plan_fwd_side);

  /* The three spectral products, folded into the full Parseval sum. */
  for(int k = 0; k < n_bins; k++) {
    double mr = spec_mid[k][0], mi = spec_mid[k][1];
    double sr = spec_side[k][0], si = spec_side[k][1];
    double w = fold[k];
    p_mm[k] = w * (mr * mr + mi * mi);
    p_ss[k] = w * (sr * sr + si * si);
    p_ms[k] = w * (mr * sr + mi * si);
  }

  /* One covariance per band, straight from the spectrum. The normalisation is
     the one np.cov and nae.cpp both use, N and N-1 with the mean already gone
     with the DC bin. */
  double cov_norm = 1.0 / ((double)n_cov * (double)(n_cov - 1));
  memset(r_mm, 0, sizeof(double) * n_bands);
  memset(r_ss, 0, sizeof(double) * n_bands);
  memset(r_ms, 0, sizeof(double) * n_bands);
  for(int k = 0; k < n_bins; k++) {
    const double *w2 = masks2 + (size_t)k * n_bands;
    double vmm = p_mm[k], vss = p_ss[k], vms = p_ms[k];
    for(int b = 0; b < n_bands; b++) {
      double w = w2[b];
      r_mm[b] += w * vmm;
      r_ss[b] += w * vss;
      r_ms[b] += w * vms;
    }
  }
  for(int b = 0; b < n_bands; b++) {
    r_mm[b] *= cov_norm;
    r_ss[b] *= cov_norm;
    r_ms[b] *= cov_norm;
  }

  /* The axes, and this window's projectors into the ring. eigen_2x2_symmetric()
     is the broadband engine's own solver, ordering the pair by eigenvalue; the
     sign it returns does not matter, v v^T being invariant under v -> -v. */
  double *slot1 = ring1 + (size_t)ring_pos * 3 * n_bands;
  double *slot2 = ring2 + (size_t)ring_pos * 3 * n_bands;
  for(int b = 0; b < n_bands; b++) {
    double eigvalues[2];
    double v1[2], v2[2];
    eigen_2x2_symmetric(r_mm[b], r_ms[b], r_ss[b], &eigvalues[0], &eigvalues[1], v1, v2);
    slot1[b] = v1[0] * v1[0];
    slot1[n_bands + b] = v1[0] * v1[1];
    slot1[2 * n_bands + b] = v1[1] * v1[1];
    slot2[b] = v2[0] * v2[0];
    slot2[n_bands + b] = v2[0] * v2[1];
    slot2[2 * n_bands + b] = v2[1] * v2[1];
  }

  /* G1(f) and G2(f) from the SUM of the last covsteps rings. Applying a filter
     is linear in the filter, so summing the last covsteps and applying once is
     the overlap-add NAE does, computed at the moment of emission -- when the
     frame has context on both sides and the circular product has somewhere to
     put its tails. Summed rather than kept as a running total: covsteps * 3 *
     n_bands additions is nothing, and a running total would drift. */
  memset(sum1, 0, sizeof(double) * 3 * n_bands);
  memset(sum2, 0, sizeof(double) * 3 * n_bands);
  for(int s = 0; s < covsteps; s++) {
    const double *q1 = ring1 + (size_t)s * 3 * n_bands;
    const double *q2 = ring2 + (size_t)s * 3 * n_bands;
    for(int i = 0; i < 3 * n_bands; i++) {
      sum1[i] += q1[i];
      sum2[i] += q2[i];
    }
  }
  /* The ring is summed BEFORE the masks are touched, not inside the loop over
     it: sum_s masks^T p_s is masks^T sum_s p_s, and doing it the other way
     round walks the whole mask array covsteps times for nothing. It was three
     quarters of the cost of the block. */
  for(int k = 0; k < n_bins; k++) {
    const double *w = masks + (size_t)k * n_bands;
    double g0 = 0.0, g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0, g5 = 0.0;
    for(int b = 0; b < n_bands; b++) {
      double wk = w[b];
      g0 += wk * sum1[b];
      g1 += wk * sum1[n_bands + b];
      g2 += wk * sum1[2 * n_bands + b];
      g3 += wk * sum2[b];
      g4 += wk * sum2[n_bands + b];
      g5 += wk * sum2[2 * n_bands + b];
    }
    gcoef[0][k] = g0;
    gcoef[1][k] = g1;
    gcoef[2][k] = g2;
    gcoef[3][k] = g3;
    gcoef[4][k] = g4;
    gcoef[5][k] = g5;
  }

  /* Apply, and back to time. The transform is unnormalised both ways, so the
     inverse divides by n_cov. */
  for(int k = 0; k < n_bins; k++) {
    double mr = spec_mid[k][0], mi = spec_mid[k][1];
    double sr = spec_side[k][0], si = spec_side[k][1];
    prod[0][k][0] = gcoef[0][k] * mr + gcoef[1][k] * sr;
    prod[0][k][1] = gcoef[0][k] * mi + gcoef[1][k] * si;
    prod[1][k][0] = gcoef[1][k] * mr + gcoef[2][k] * sr;
    prod[1][k][1] = gcoef[1][k] * mi + gcoef[2][k] * si;
    prod[2][k][0] = gcoef[3][k] * mr + gcoef[4][k] * sr;
    prod[2][k][1] = gcoef[3][k] * mi + gcoef[4][k] * si;
    prod[3][k][0] = gcoef[4][k] * mr + gcoef[5][k] * sr;
    prod[3][k][1] = gcoef[4][k] * mi + gcoef[5][k] * si;
  }
  for(int i = 0; i < 4; i++)
    fftw_execute(plan_inv[i]);

  /* The frame the base class is about to emit, unnormalised by covsteps+1 --
     that division belongs to emitBlock(), as it does for the broadband engine. */
  double scale = 1.0 / (double)n_cov;
  for(int i = 0; i < sample_count; i++) {
    pca.c1_mid[i] = out_time[0][syn_offset + i] * scale;
    pca.c1_side[i] = out_time[1][syn_offset + i] * scale;
    pca.c2_mid[i] = out_time[2][syn_offset + i] * scale;
    pca.c2_side[i] = out_time[3][syn_offset + i] * scale;
  }
}

void NaeErb::advanceBlock(void)
{
  if(!erb_ready)
    return;

  /* The history, on by a frame. memmove and not a ring index: the transform
     wants its input contiguous, and moving 3072 doubles twice a period is a
     few microseconds against a period of five milliseconds. */
  int keep = n_cov - sample_count;
  memmove(mid_hist, mid_hist + sample_count, sizeof(double) * keep);
  memmove(side_hist, side_hist + sample_count, sizeof(double) * keep);
  memset(mid_hist + keep, 0, sizeof(double) * sample_count);
  memset(side_hist + keep, 0, sizeof(double) * sample_count);

  ring_pos = (ring_pos + 1) % covsteps;
}
