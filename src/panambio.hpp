/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _PANAMBIO_HPP_
#define _PANAMBIO_HPP_

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
#define PANCOEFF -2.5

typedef struct {
  double *sum_xy_array;
  double *sum_x2_array;
  double *sum_y2_array;
  double *sum_x_array;
  double *sum_y_array;
} CovMatrix;


typedef struct {
  double *mid_step;
  double *side_step;
  double *mid_left;
  double *mid_right;
  double *side_left;
  double *side_right;
} PCATrans;

//void parse_error(const char msg[]);

//void parse_error_exit(const char msg[]);

//double covariance(const float* x, const float* y, int N);
//double correlation(const float* x, const float* y, int N);
int eigen_2x2_symmetric(double a, double b, double d,double* eig1, double* eig2, double v1[2], double v2[2]);
//static void* process_front(void *n_panambio);
//static void* process_surround(void *n_panambio);

class Panambio {

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
  double gain_main;
  double gain_amb;
  double gain_main_surround;
  int overlap;
  double *comps;
  CovMatrix covM;
  CovMatrix icorrv;
  PCATrans pca;
  double pan;
  double icorr;
  float *left_in;
  float *right_in;
  float *left_out;
  float *right_out;
  float *mid_left_out;
  float *side_left_out;
  float *mid_right_out;
  float *side_right_out;
  string left_name_in;
  string right_name_in;
  string left_name_out;
  string right_name_out;
  string mid_left_name_out;
  string mid_right_name_out;
  string side_left_name_out;
  string side_right_name_out;

public:
  
  Panambio(string n_name, int n_mode);
  ~Panambio(void);

  void setQuiet(void) { quiet = true; };
  string getName(void) { return name; };
  bool setMainGain(double gain);
  bool setAmbGain(double gain);
  bool setSurrGain(double gain);
  void setSampleCount(int n_sample_count);
  void setStepsLength(int n_steps_length);
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
