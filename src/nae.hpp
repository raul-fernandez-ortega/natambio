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

#include "structs.hpp"

using namespace std;

#define ICORRL 20

/* Step of the lag sweep of <projection>, in samples. The sweep runs from 0 to
 * one period (sample_count), which is the largest delay the buffers can reach
 * back to. Sweeping every single lag would cost covsteps*sample_count
 * multiply-adds per lag per channel -- around 21 million per period at 1024
 * frames and covsteps 5, which does not fit in the period's deadline. At a
 * step of 50 samples (about 1.1 ms) it is roughly 430 thousand, which does. */
#define NAE_PROY_LAG_STEP 50


typedef struct {
  double *sum_xy_array;
  double *sum_x2_array;
  double *sum_y2_array;
  double *sum_x_array;
  double *sum_y_array;
} RunningSums;


/* Per-channel state of the <projection> study: the least-squares projection of
 * the delayed C1 onto C2.
 *
 * The delay never enters the signal itself. With tau the lag that maximises the
 * correlation, the projection is P[n] = a*(C1[n-tau] - mean), so the copy with
 * the delay taken back out is simply P_und[n] = a*(C1[n] - mean): tau only
 * decides which stretch of C1 the coefficient was fitted on. That is what keeps
 * the whole thing causal -- taking the projection out of C1 needs no look-ahead
 * at all, and putting it into the ambience still delayed reaches at most tau
 * samples back, which the history already holds. <projection> therefore adds no
 * latency.
 *
 * hist_* run on one timeline of proy_length = (covsteps+1)*sample_count
 * samples, oldest first, shifted left by one period per cycle: the newest block
 * is the one being emitted, and the covsteps*sample_count analysis window ends
 * on it, with one period of head room underneath for the lag sweep. */
typedef struct {
  double *hist_c1;     // finalised C1, as the engine emits it
  double *hist_c2;     // finalised C2, same timeline
  double *pref_x;      // prefix sums of hist_c1, so every lag costs O(1) here
  double *pref_x2;     // prefix sums of hist_c1^2
  double a_cur;        // this cycle's fit
  double mean_cur;
  int lag_cur;
  double a_prev;       // last cycle's fit, held for the cross-fade
  double mean_prev;
  int lag_prev;
} ProyChannel;


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

private:

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
  double pan_scale;    // configured width of the input pair, [-1, 1]
  double pan_a;        // same-channel weight of the width matrix
  double pan_b;        // opposite-channel weight of the width matrix
  int covsteps;
  RunningSums covM;
  RunningSums icorrv;
  PCATrans pca;
  double side_weight;
  double icorr;
  bool projection;     // <projection>: project the delayed C1 onto C2
  double proy_gain;    // <proy_gain>: gain of the projection summed into the ambience
  int proy_mode;       // <proy_mode>: 0 = delayed (C2's timebase), 1 = undelayed
  int proy_lag_max;    // largest lag swept, one period
  int proy_length;     // length of the ProyChannel history arrays
  double *proy_fade;   // raised-cosine ramp, one period long, joining successive fits
  ProyChannel proy[2]; // 0 = left, 1 = right
  float *left_in;
  float *right_in;
  float *left_out;
  float *right_out;
  float *c1_left_out;
  float *c2_left_out;
  float *c1_right_out;
  float *c2_right_out;
  string left_name_in;
  string right_name_in;
  string left_name_out;
  string right_name_out;
  string c1_left_name_out;
  string c1_right_name_out;
  string c2_left_name_out;
  string c2_right_name_out;

public:
  
  NAE(string n_name, int n_mode);
  ~NAE(void);

  void setQuiet(void) { quiet = true; };
  string getName(void) { return name; };
  bool setC1Gain(double gain);
  bool setC2Gain(double gain);
  bool setC2RearGain(double gain);
  void setPanScale(double n_scale);
  void setSampleCount(int n_sample_count);
  void setCovStepsLength(int n_covsteps);
  void setProjection(bool n_projection, double n_proy_gain, int n_proy_mode);
  bool getProjection(void) { return projection; };
  void setChannelIn(enum side n_side, string n_channel_in);
  void setChannelOut(enum side n_side, string n_channel_out);
  string getChannelIn(enum side n_side);
  string getChannelOut(enum side n_side);
  void fillInputBuffer(enum side n_side, const float *n_input);
  void fillOutputBuffer(enum side n_side, float *n_output);
  void load(int abspri, int policy);
  void signal(void);
  void thr_process(void);
  void proy_process(void);
  
};

#endif
