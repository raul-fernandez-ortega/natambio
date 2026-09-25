/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _NAE_HPP_
#define _NAE_HPP_

#ifdef __cplusplus
extern "C" {

#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

#endif 

#ifdef __cplusplus
}
#endif

#include <math.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <cstring>

#include <atomic>

#include "structs.hpp"
#include "cycletime.hpp"

using namespace std;

#define ICORRL 20

/* The default reconstruction window, when <steps_length> and <steps_length_ms>
   are both absent: three blocks at a REFERENCE period of 256, rounded up to
   whole blocks at whatever period JACK is really running.

       steps_length = ceil(NA_NAE_STEPS_REF_BLOCKS * NA_NAE_STEPS_REF_FRAMES
                           / jack_frame_size)

   Expressed that way and not as a plain number because the old default -- a
   flat 5 -- gave a DIFFERENT LATENCY at every period: 26.7 ms at 256 frames,
   13.3 ms at 128, for one and the same file. This keeps the window at 768
   samples wherever the period divides it, which is 16 ms at 48 kHz.

   It is invariant in SAMPLES, not in time: at 44.1 kHz those 768 samples are
   17.4 ms and at 96 kHz they are 8. Making it invariant in time instead is one
   constant away -- see <steps_length_ms>, which is the same arithmetic -- and
   was not done because the reconstruction window is the overlap-add's, and the
   overlap-add counts blocks. */
#define NA_NAE_STEPS_REF_BLOCKS  3
#define NA_NAE_STEPS_REF_FRAMES  256

/* Bounds on a NAE gain, in dB: the same window the port gains use, and there
   for the same reason -- these come in over the network from anything that can
   reach the socket, and a component driven far up is a driver at risk. The
   floor is where a gain stops meaning anything and starts meaning silence. */
#define NA_NAE_GAIN_MAX_DB    20.0
#define NA_NAE_GAIN_MIN_DB  -120.0

/* The ends of <pan_scale>: +1 is the pair collapsed to mono, -1 is the two
   channels in opposite polarity, 0 is the signal untouched. Unlike the gain
   clamps this is not a safety margin but the domain of the parameter -- there
   is no width beyond mono -- so a value outside it is refused rather than
   clamped, as naconf refuses one in the file. */
#define NA_NAE_PAN_MAX         1.0
#define NA_NAE_PAN_MIN        -1.0

/* WHERE THE AMBIENCE STOPS BEING AMBIENCE.
 *
 * C2 comes out as a stereo pair like anything else, and when the two channels
 * of it sit at very different levels what is in there is no longer a diffuse
 * field: it is a source that happens to have landed in the second component.
 * So C2 is cut in two. The ambience keeps whatever difference it is allowed to
 * carry, NA_NAE_LAT_THRESHOLD_DB of it; everything the louder channel has
 * beyond that goes to a pair of its own, with the quieter channel at zero --
 * exactly zero, except while a swing is crossing over and the two factors are
 * both on the move. emitBlock() has the measurement.
 *
 * It is not a rare corner, and how often it fires says something about the
 * recording. Of the energy of C2, the cut sends to the lateral pair 33 % on
 * "I Am In Love" and 27 % on "Please Please Me" -- primitive stereo, an
 * instrument panned hard to one side and landing in the second component --
 * against 0.6 % on Ravel and 0.5 % on modern pop, where the stereo image is
 * built properly and there is nothing lateral in the ambience to take out. It
 * engages on the material it is for and stays out of the way on the rest.
 *
 * Why it runs so large: C2 is rank one within a block, so the difference
 * between its channels IS the ambient eigenvector's lean off pure side -- 4.8
 * dB at 15 degrees, 11.4 at 30. docs/nae has the table.
 *
 * THE CUT IS COMPLEMENTARY IN AMPLITUDE and that is the whole safety argument:
 * the two halves are one waveform scaled by a and by 1-a, so
 *
 *     ambient + lateral = C2,  sample for sample, whatever a is.
 *
 * Nothing is invented and nothing is lost; a detector that gets the level
 * wrong misroutes signal between two pairs and can do nothing worse than that.
 * Complementary in POWER (a and sqrt(1-a^2)) would be the wrong arithmetic
 * here -- the two halves are perfectly correlated, being the same samples, so
 * they add coherently and that pair would sum to as much as +3 dB.
 *
 * With <lateral_gain> set to the gain the mode already carries the ambience
 * with, the two halves go back out at one gain and the engine is exactly what
 * it was before any of this existed.
 *
 * THE KNEE. The reduction applied to the louder channel, in dB, against the
 * measured difference d, with T the threshold and W the knee:
 *
 *     G(d) = 0                        d <= T - W/2
 *          = -(d - T + W/2)^2 / (2W)  inside the knee
 *          = -(d - T)                 d >= T + W/2
 *
 * C1 continuous, degenerating to the bare corner at W = 0, and for a large
 * difference it leaves the ambience at exactly T. The knee is what keeps a
 * signal hovering around the threshold from riding a corner in the curve.
 *
 * W < 2T, AND IT IS NOT A STYLE POINT. At W = 2T the bottom of the knee
 * reaches 0 dB, and below that BOTH channels would be attenuated at once --
 * the lateral pair would stop having a channel at zero and become a second
 * copy of the ambience. It is refused at parse time and again in
 * setLateralSplit(), not clamped.
 *
 * T and W come from <lateral_threshold_db> and <lateral_knee_db>; the two
 * constants below are only their defaults. The knee also decides how often the
 * lateral pair has both channels at once: the overlap is a product of the knee
 * keeping both factors under 1 more of the time, and a hard corner (W = 0)
 * keeps it to the least. */
/* The defaults for <lateral_threshold_db> and <lateral_knee_db>. They are
   defaults and no longer the whole story: how much a given threshold takes
   depends on the recording far more than it looks -- 33 % of the ambience on
   "I Am In Love" against 0.6 % on Ravel, at the same 5 dB. A number that
   material-dependent belongs in the file. */
#define NA_NAE_LAT_THRESHOLD_DB   5.0
#define NA_NAE_LAT_KNEE_DB        5.0
/* WHAT IT COSTS. Two passes over the frame where there was one, the second of
 * them a sum of squares, plus a logarithm and two knee evaluations a block.
 * Measured with nae_bench at 48 kHz, 256 frames, covsteps 3: from 26.4 to
 * 33.5 us a block. */

/* The floor under the two level estimates. It is there for silence, where the
   ratio of two zeroes is whatever the last denormal says: with this in both
   terms a silent block reads as no difference at all, which puts the whole of
   nothing into the ambience. In output units squared, so 1e-20 is an amplitude
   of 1e-10 -- two hundred dB below anything that is signal. */
#define NA_NAE_LAT_EPS            1e-20



typedef struct {
  double *sum_xy_array;
  double *sum_x2_array;
  double *sum_y2_array;
  double *sum_x_array;
  double *sum_y_array;
} RunningSums;


typedef struct {
  double *mid_step;   // mid of the input pair, L+R
  double *side_step;  // side of the input pair, L-R
  double *c1_mid;     // mid coordinate of the principal component
  double *c1_side;    // side coordinate of the principal component
  double *c2_mid;     // mid coordinate of the ambience component
  double *c2_side;    // side coordinate of the ambience component
} PCATrans;

int eigen_2x2_symmetric(double a, double b, double d,double* eig1, double* eig2, double v1[2], double v2[2]);

class NAE {

  /* Protected rather than private: an engine that decomposes differently
     is a subclass, and it needs the buffers, the gains, the width
     and the timer -- everything the block is made of except the decomposition
     itself. */
protected:

  string name;
  sem_t semaphore;
  pthread_attr_t attr;
  pthread_mutex_t  mutex;
  pthread_t t_proc;
  struct sched_param parm;
  int prio;
  bool quiet;
  bool run;
  int mode; // 0 = Front 1 = Rear
  int sample_count;
  double gain_c1;
  double gain_c2;
  double gain_c2_rear;
  double gain_lat;
  /* Width of the input pair. pan_scale is the number the configuration and the
     remote manager work in and belongs to whichever thread set it; the target
     is that same number and the ONE word the worker reads from outside -- the
     scalar and not the two weights, so that the worker can never see half a
     change and derive a matrix from a pair of numbers that never went
     together. pan_scale_now is where the worker has slewed to, and pan_a/pan_b
     the weights of that. */
  double pan_scale;    // configured width of the input pair, [-1, 1]
  volatile float pan_scale_target;
  double pan_scale_now;
  double pan_a;        // same-channel weight of the width matrix
  double pan_b;        // opposite-channel weight of the width matrix
  /* Where the width matrix starts this block and how much it moves per sample.
     prepareBlock() computes them and commits pan_a/pan_b to where the block
     ends, so a decomposition walks from *_begin by *_step and never has to know
     how the slew was worked out. */
  double pan_a_begin;
  double pan_b_begin;
  double pan_a_step;
  double pan_b_step;
  int covsteps;
  int sample_rate;
  /* Where a gain change is heard as a fade rather than a step. The three
     numbers per gain are the discipline ioJack keeps for the port gains
     (iojack.hpp): <name>_db is what the configuration and the remote manager
     work in and belongs to whichever thread set it; <name>_target is that same
     number, linear, and the only one the worker thread reads from outside;
     <name> is where the worker has got to, slewed towards the target across
     each block. One writer each. */
  double gain_c1_db;
  double gain_c2_db;
  double gain_c2_rear_db;
  double gain_lat_db;
  volatile float gain_c1_target;
  volatile float gain_c2_target;
  volatile float gain_c2_rear_target;
  volatile float gain_lat_target;
  /* Per-sample slew, from the sample rate: the rate ioJack fades at, so a NAE
     gain and a port gain arriving together move as one. */
  float ramp_inc;
  /* How long one block of the decomposition takes, cycle by cycle: the whole
     of what this thread does between two semaphore signals. It is the figure
     the "timecycle" command reports for this engine, and the one that says
     whether the engine keeps up -- the callback signals it once per period and
     does not wait for it, so a block that takes longer than a period is an
     engine falling behind rather than an xrun. */
  CycleTimer proc_time;
  /* What proc_time cannot see: whether the block STARTED on time. The timer is
     opened after sem_wait() returns, so an engine that runs a period late still
     reports a short block -- the work is the same work, it is simply being done
     for a period that has already gone out. The semaphore counts the signals
     the worker has not consumed yet, so its value read straight after a wait is
     the backlog in blocks: 0 when the engine is keeping up, and one more for
     every period it is behind. It matters because falling behind is SILENT --
     the callback reads whatever emitBlock() published last, so a late engine
     repeats the previous block rather than reporting anything, and a repeated
     block is a step in the output where the signal had none. Which is what a
     click is. */
  std::atomic<unsigned long long> late_blocks;
  std::atomic<unsigned long long> late_total;
  std::atomic<unsigned int> late_max;
  RunningSums covM;
  RunningSums icorrv;
  PCATrans pca;
  /* THE LEVEL THE LATERAL CUT IS DECIDED ON, over the window the PCA itself
     works on: covsteps frames, one energy per frame per channel, summed. A
     level is not a sample -- comparing |left| against |right| sample by sample
     is not a measurement of anything, it is a multiplication by a waveform,
     and what comes out of it is distortion. So the two channels are measured
     over the same span the axis was estimated over and the answer moves once
     per block.

     Written and summed in emitBlock() and nowhere else, ring and all, so that
     an engine that overrides advanceBlock() without chaining to this one
     still gets it.

     The energies are of the RECONSTRUCTED pair, the one about to be emitted,
     and not of pca.c2_mid / c2_side as they stand. Those are mid-overlap-add:
     the frame at [0, sample_count) has had all covsteps contributions and the
     tail of the buffer has had one, so a mean taken across the whole of it
     would weight a half-built tail against a finished head and bias the very
     ratio this is here to measure. The ring costs one pass over a frame and
     every sample in it is finished. It lags the analysis window by covsteps-1
     frames, which is the engine's own reconstruction latency and exactly
     right: what is being decided is the level of the audio being cut, not of
     the audio being analysed. */
  double *lat_pow_l;   /* covsteps, energy per frame, left */
  double *lat_pow_r;   /* covsteps, energy per frame, right */
  /* Where the two cut factors have slewed to, left and right. At most one of
     them is ever below 1 -- only the louder channel is cut -- and they move on
     ramp_inc like every other gain here, so the factor is continuous across a
     block boundary as well as within a block. */
  double lat_a_left;
  double lat_a_right;
  /* <lateral_threshold_db> and <lateral_knee_db>, the curve latFactor() walks.
     Three values for the pair, the discipline pan_scale keeps: these two are
     what the configuration and the remote manager work in and belong to
     whichever thread set them, lat_split_target is the one word the worker
     reads from outside, and the _now pair is where the worker has got to.
     One writer each. */
  double lat_threshold_db;
  double lat_knee_db;
  /* THE PAIR AS ONE WORD, and it has to be one. Read as two, the worker could
     take a threshold from before a change and a knee from after -- a pair that
     was never set together, and one that can break knee < 2*threshold and so
     put both channels of the lateral pair under a cut at once. pan_scale is
     kept this way for exactly that reason; the difference is only that there
     the single word was already a scalar and here two have to be packed into
     one. Floats, like every other target here: seven digits is more dB than
     anyone can hear the difference of. */
  std::atomic<unsigned long long> lat_split_target;
  double lat_threshold_now;
  double lat_knee_now;

  static unsigned long long packSplit(double t, double w)
  {
    float tf = (float)t, wf = (float)w;
    unsigned int a, b;
    memcpy(&a, &tf, sizeof(a));
    memcpy(&b, &wf, sizeof(b));
    return ((unsigned long long)a << 32) | (unsigned long long)b;
  }
  static void unpackSplit(unsigned long long v, double *t, double *w)
  {
    unsigned int a = (unsigned int)(v >> 32);
    unsigned int b = (unsigned int)(v & 0xffffffffull);
    float tf, wf;
    memcpy(&tf, &a, sizeof(tf));
    memcpy(&wf, &b, sizeof(wf));
    *t = (double)tf;
    *w = (double)wf;
  }
  double side_weight;
  double icorr;
  float *left_in;
  float *right_in;
  /* THE OUTPUT HANDOFF IS LOCK FREE, and it has to be. These six buffers are
     written by this engine's worker and read by the JACK process thread, and
     both are real-time threads. They used to be handed over under a mutex,
     which meant the audio callback could block on a worker that had been
     preempted while holding it -- for as long as the scheduler took to come
     back, with no bound. JACK reports that as "client was not finished" and
     stops the graph, which is what the journal of 2026-09-06 00:14 shows: an
     xrun a second, none of them the DSP's fault, none of them recoverable.
     Priority inheritance would not have saved it either, the worker running at
     exactly the callback's priority and so having nothing to inherit.

     So: two sets of buffers, and an index saying which one is complete. The
     worker fills the set the callback is not reading and then publishes it in
     one atomic store; the callback reads the index once and sums from that set.
     Nobody waits for anybody. Two sets are enough because the worker publishes
     at most once per period and the callback reads within one. */
  float *left_out[2];
  float *right_out[2];
  float *c1_left_out[2];
  float *c2_left_out[2];
  float *c1_right_out[2];
  float *c2_right_out[2];
  float *lat_left_out[2];
  float *lat_right_out[2];
  std::atomic<int> out_pub;     /* the set the callback should read */
  string left_name_in;
  string right_name_in;
  string left_name_out;
  string right_name_out;
  string c1_left_name_out;
  string c1_right_name_out;
  string c2_left_name_out;
  string c2_right_name_out;
  string lat_left_name_out;
  string lat_right_name_out;

  /* One block of slew towards a gain's target, at most ramp_inc per sample:
     the same step ioJack::slewGain() takes, on the same clock, so a change
     lands in a few tens of milliseconds and no faster than the ear forgives.
     Returns where the gain gets to by the end of this block. */
  double slewGain(double current, double target) {
    double span = (double)ramp_inc * (double)sample_count;
    if(target > current) {
      current += span;
      if(current > target) current = target;
    } else if(target < current) {
      current -= span;
      if(current < target) current = target;
    }
    return current;
  }

  /* The three gains addressed as one, so that the setter, the reporter and the
     mode test are each written once instead of three times over. */
  double *gainDbSlot(enum nae_gain which);
  volatile float *gainTargetSlot(enum nae_gain which);

public:
  
  NAE(string n_name, int n_mode);
  /* Virtual because ioJack keeps vector<NAE*> and deletes through it: with a
     derived engine in that vector, a non-virtual destructor would run only the
     base's and leave the derived part -- FFTW plans and all -- unfreed, which
     is undefined behaviour and not merely a leak. */
  virtual ~NAE(void);

  void setQuiet(void) { quiet = true; };
  string getName(void) { return name; };
  /* 0 = alpha (front), 1 = beta (rear), as <mode> spelled them. */
  int getMode(void) { return mode; };
  /* The configuration's three gains, LINEAR, as <front_gain> and the rest are
     parsed into. They set the target and the current value alike: the engine
     starts at its configured gain rather than ramping up to it from silence on
     the first block. */
  bool setC1Gain(double gain);
  bool setC2Gain(double gain);
  bool setC2RearGain(double gain);
  bool setLatGain(double gain);
  /* The threshold and the knee, in dB, as <lateral_threshold_db> and
     <lateral_knee_db> are parsed. Refused, and nothing touched, outside the
     domain -- see NA_NAE_LAT_THRESHOLD_DB for what the domain is and why the
     knee cannot reach twice the threshold. naconf refuses the same values
     before they ever get here, as it does for <pan_scale>; this is the guard
     for a caller that builds an engine by hand. */
  bool setLateralSplit(double threshold_db, double knee_db);
  /* The same pair for the remote manager, and the reason it is live at all:
     how much a given threshold takes depends on the recording, so it is a
     number to be found by ear over several of them, and a restart between two
     of those is a comparison nobody can make. Safe from any thread but the
     worker's; it writes one word.

     NOT SLEWED, and it does not need to be. What the worker does with these is
     work out where the cut factor should be, and the FACTOR is already slewed
     on ramp_inc like every gain here -- so a threshold moved while the music
     plays is heard as the same short fade a gain would be, without this having
     to ramp anything of its own. */
  bool setLiveLateralSplit(double threshold_db, double knee_db);
  double getLatThresholdDb(void) const { return lat_threshold_db; };
  double getLatKneeDb(void) const { return lat_knee_db; };

  /* The same three for the remote manager, in dB and one at a time.
     setGainDb() clamps to [NA_NAE_GAIN_MIN_DB, NA_NAE_GAIN_MAX_DB] and returns
     what it settled on; the worker thread slews to it, so the change is a fade
     and not a step. Safe to call from any thread but the worker's: it writes
     the dB value and then, in one store, the single word the worker reads.
     gainActive() says whether this mode uses that gain at all -- a value set on
     one it does not is kept and reported, it simply multiplies nothing. */
  double setGainDb(enum nae_gain which, double db);
  double gainDb(enum nae_gain which);
  bool gainActive(enum nae_gain which);

  /* Configuration time: the width as it starts, weights and all, with nothing
     to slew from. */
  void setPanScale(double n_scale);
  /* The same for the remote manager, slewed like a gain: a width applied whole
     is a jump in the matrix the pair is multiplied by, which is as much a click
     as a jump in a gain. False, and nothing changed, outside [-1, 1]. Safe from
     any thread but the worker's; it writes one word. */
  bool setLivePanScale(double n_scale);
  /* <pan_scale> and <steps_length> as configured, for a caller writing the
     engine's configuration back out. The width matrix derived from pan_scale
     is not reported: it is two weights computed from this one number, and the
     number is what the file holds. */
  double getPanScale(void) { return pan_scale; };
  int getCovStepsLength(void) { return covsteps; };
  /* What the engine costs in latency, in frames: the reconstruction window,
     which is covsteps periods long, because a block cannot be emitted until the
     overlap-add that spans it has finished. <steps_length> is the only tag
     that moves it. Virtual because an engine that reconstructed differently
     would answer differently; this one does not. */
  virtual int latency(void) const { return covsteps * sample_count; };
  void setSampleCount(int n_sample_count);
  /* For the gain ramp; JACK's rate, taken once the client is open. */
  void setSampleRate(int n_sample_rate);
  void setCovStepsLength(int n_covsteps);
  void setChannelIn(enum side n_side, string n_channel_in);
  void setChannelOut(enum side n_side, string n_channel_out);
  string getChannelIn(enum side n_side);
  string getChannelOut(enum side n_side);
  void fillInputBuffer(enum side n_side, const float *n_input);
  void fillOutputBuffer(enum side n_side, float *n_output);
  /* The per-block times, for the remote manager: read from its thread, never
     from the worker's, and racing with the worker no more than any other
     reader of a running timer does (cycletime.hpp). */
  /* Whether this engine has a principal component to hand out in beta mode.
     The broadband engine does not: its decompose() takes the beta branch and
     accumulates the second axis alone, C1 being work nobody had asked for.
     An engine that computed C1 on the way to the ambience would have it for
     the cost of the store, and would answer true. An engine that answers
     false leaves the C1 buffers as calloc left them, which is silence, and
     natambio.cpp says so at load time rather than let a configuration connect
     a port that will never carry anything. */
  virtual bool c1InBeta(void) const { return false; };
  void timeStats(struct na_time_stats *st) { proc_time.stats(st); };
  void resetTimeStats(void) { proc_time.reset(); };
  /* The backlog, for the same reader: how many blocks have been started late,
     out of how many were started at all, and the worst backlog seen. Relaxed
     loads -- three counters that are only ever read together to be printed, and
     a report that catches one of them a block later than the others says the
     same thing about an engine as one that does not. */
  unsigned long long lateBlocks(void) const
    { return late_blocks.load(std::memory_order_relaxed); };
  unsigned long long totalBlocks(void) const
    { return late_total.load(std::memory_order_relaxed); };
  unsigned int lateMax(void) const
    { return late_max.load(std::memory_order_relaxed); };
  void resetLate(void)
  {
    late_blocks.store(0, std::memory_order_relaxed);
    late_total.store(0, std::memory_order_relaxed);
    late_max.store(0, std::memory_order_relaxed);
  };

  void load(int abspri, int policy);
  void signal(void);
  void thr_process(void);

protected:

  /* One period, in four steps, in this order and no other.
   *
   * prepareBlock()  the width across the block and, in beta, the correlation
   *                 that sets side_weight. Both engines want exactly this, so
   *                 it is not virtual.
   * decompose()     VIRTUAL, and the only thing that differs between engines:
   *                 take the period that has just arrived, estimate whatever
   *                 axis the engine estimates, and leave the components of the
   *                 frame about to be emitted in pca.c1_mid / c1_side / c2_mid
   *                 / c2_side over [0, sample_count), UNNORMALISED -- the
   *                 division by covsteps+1 belongs to the step below.
   * emitBlock()     the gains, slewed, and the write to the output buffers
   *                 under the mutex. Identical in both engines.
   * advanceBlock()  VIRTUAL: the engine's own buffers, shifted on by a frame.
   *                 It runs AFTER emitBlock() because emitBlock() reads the
   *                 frame this is about to shift out from under it.
   */
  void prepareBlock(void);
  virtual void decompose(void);
  void emitBlock(void);
  virtual void advanceBlock(void);

  /* The cut factor for one channel: what the louder of the pair is multiplied
     by so that the ambience is left carrying NA_NAE_LAT_THRESHOLD_DB of
     difference and no more. <diff_db> is this channel's level above the other
     one; at or below the knee it returns exactly 1 and nothing is cut. The
     curve is written out in the constants above. */
  double latFactor(double diff_db) const;

};

#endif
