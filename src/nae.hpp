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
  void setChannelIn(enum side n_side, string n_channel_in);
  void setChannelOut(enum side n_side, string n_channel_out);
  string getChannelIn(enum side n_side);
  string getChannelOut(enum side n_side);
  void fillInputBuffer(enum side n_side, const float *n_input);
  void fillOutputBuffer(enum side n_side, float *n_output);
  void load(int abspri, int policy);
  void signal(void);
  void thr_process(void);
  
};

#endif
