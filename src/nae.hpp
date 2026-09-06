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
     (nae_erb.hpp) is a subclass, and it needs the buffers, the gains, the width
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
  volatile float gain_c1_target;
  volatile float gain_c2_target;
  volatile float gain_c2_rear_target;
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
  std::atomic<int> out_pub;     /* the set the callback should read */
  string left_name_in;
  string right_name_in;
  string left_name_out;
  string right_name_out;
  string c1_left_name_out;
  string c1_right_name_out;
  string c2_left_name_out;
  string c2_right_name_out;

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
  /* The tag this engine is declared with, for a report that has to write the
     configuration back out. Virtual and not a flag because what the caller
     actually wants is the word to print. */
  virtual const char *engineTag(void) { return "nae"; };
  /* 0 = alpha (front), 1 = beta (rear), as <mode> spelled them. */
  int getMode(void) { return mode; };
  /* The configuration's three gains, LINEAR, as <front_gain> and the rest are
     parsed into. They set the target and the current value alike: the engine
     starts at its configured gain rather than ramping up to it from silence on
     the first block. */
  bool setC1Gain(double gain);
  bool setC2Gain(double gain);
  bool setC2RearGain(double gain);

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
     The ERB engine always has it -- it computes C1 and gets the ambience by
     subtracting it -- so there the tap costs nothing but the store. An engine
     that answers false leaves the C1 buffers as calloc left them, which is
     silence, and natambio.cpp says so at load time rather than let a
     configuration connect a port that will never carry anything. */
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

  /* Virtual so a subclass can build what it needs -- a filter bank, a set of
     FFTW plans -- before the thread exists, which is the only moment at which
     the rate, the period and the covariance length are all known and nothing
     is running yet. */
  virtual void load(int abspri, int policy);
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

};

#endif
