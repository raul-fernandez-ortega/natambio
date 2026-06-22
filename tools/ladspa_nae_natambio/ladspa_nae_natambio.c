// -----------------------------------------------------------------------------
//
//  ladspa_nae_natambio -- LADSPA plugin port of the natambio NAE engine.
//
//  This is an almost-direct translation of the PCA-based spatial decomposition
//  implemented in natambio's src/nae.cpp. It runs the same 2x2 covariance
//  eigenvalue solver over an overlapping window (COVSTEPS buffers) to split a
//  stereo signal into a "main" (front) component and an "ambience" / surround
//  component.
//
//  It is intended for initial tests, algorithm verification, and possible
//  real-time use with LADSPA-compatible hosts (e.g. ecasound). Build with
//  -DDEBUG to print a per-block correlation table (see Makefile.simple
//  DEBUG=1).
//
//  Author : Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
//  License: GPLv3
//
// -----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ladspa.h>

#define PLUGIN_ID 1
#define PLUGIN_NAME "NAE_NATAMBIO"
#define PLUGIN_LABEL "NAE_NATAMBIO"
#define PLUGIN_MAKER "Raul Fernandez Ortega"
#define PLUGIN_COPYRIGHT "GPLv3"
#define ICORRL 20
#define NATAMBCOEFF -2.5
#define COVSTEPS 5

// Total ports: 2 inputs + 2 outputs + 4 parameters = 8
#define NUM_PORTS 8

#ifndef M_PI
#define M_PI ((double) 3.14159265358979323846264338327950288)
#endif

#ifndef M_2PI
#define M_2PI ((double) 6.28318530717958647692528676655900576)
#endif

#ifndef FROM_DB
#define FROM_DB(db) (pow(10, (db) / 20.0))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

// Control-port ranges. These MUST match the LADSPA_PortRangeHint bounds declared
// in init() below: runtime values read from the host are clamped to them, so a
// misbehaving host or parameter automation can never push a control out of range.
#define PARAM_MODE_MIN      0.0
#define PARAM_MODE_MAX      1.0
#define PARAM_GAIN_DB_MIN (-10.0)
#define PARAM_GAIN_DB_MAX  10.0
#define PARAM_DEFAULT       1.0   // mode default (1 = ambience); matches LADSPA_HINT_DEFAULT_1
#define PARAM_GAIN_DEFAULT  0.0   // gain default, 0 dB = unity; matches LADSPA_HINT_DEFAULT_0

// Clamp a double to [lo, hi].
static inline double clampd(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Read a control port (may be unconnected -> use default) and clamp to [lo, hi].
static inline double read_ctrl(const LADSPA_Data *port, double dflt, double lo, double hi) {
  return clampd(port ? (double)*port : dflt, lo, hi);
}

// Port indices
enum {
  PORT_INPUT1,
  PORT_INPUT2,
  PORT_OUTPUT1,
  PORT_OUTPUT2,
  PORT_PARAM1,
  PORT_PARAM2,
  PORT_PARAM3,
  PORT_PARAM4
};

typedef struct {
  double *sum_xy_array;
  double *sum_x2_array;
  double *sum_y2_array;
  double *sum_x_array;
  double *sum_y_array;
} CovMatrix;

typedef struct {
  LADSPA_Data *mid_step;
  LADSPA_Data *side_step;
  LADSPA_Data* mid_left;
  LADSPA_Data* mid_right;
  LADSPA_Data* side_left;
  LADSPA_Data* side_right;
} PCATrans;

typedef struct {
  int mode;
  unsigned long frame_size;
  unsigned long rate;
  double gain_main;
  double gain_amb;
  double gain_main_surround;
  int covsteps;
  CovMatrix covM;
  PCATrans pca;
  LADSPA_Data *ports[NUM_PORTS];
  double beta;
  CovMatrix icorrv;
  double icorr;
#ifdef DEBUG
  unsigned long step_count;
#endif
} CustomPlugin;

// Compute signal level in dB
double leveldB(const LADSPA_Data* x, size_t N) {
  double level = 0;
  for (size_t i = 0; i < N; i++) {
      level += x[i]*x[i];
  }
  return 10*log10(level/(double)N);
}

// Compute signal level
double level(const LADSPA_Data* x, size_t N) {
  double level = 0;
  for (size_t i = 0; i < N; i++) {
      level += x[i]*x[i];
  }
  return level/(double)N;
}

// Compute covariance between two double vectors of length N
double covariance(const LADSPA_Data* x, const LADSPA_Data* y, size_t N) {
    if (N == 0) return 0.0;

    double mean_x = 0.0, mean_y = 0.0;

    // First pass: compute means
    for (size_t i = 0; i < N; i++) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= N;
    mean_y /= N;

    double cov = 0.0;
    for (size_t i = 0; i < N; i++) {
      cov += (x[i] - mean_x) * (y[i] - mean_y);
    }

    return cov / (N-1); // Use N-1 for sample covariance
}

// Compute correlation between two float vectors of legnth N by Pearson algorithm
double correlationPearson(const LADSPA_Data* x, const LADSPA_Data* y, size_t N) {
    if (N == 0) return 0.0;

    double sumx = 0.0;
    double sumy = 0.0;
    double sumxy = 0.0;
    double sumx2 = 0.0;
    double sumy2 = 0.0;

    for (size_t i = 0; i < N; i++) {
      sumx += x[i];
      sumy += y[i];
      sumx2 += x[i]*x[i];
      sumy2 += y[i]*y[i];
      sumxy += x[i]*y[i];
    }
    if(N*sumx2==sumx*sumx || N*sumy2==sumy*sumy)
      return 1;
    else
      return (N*sumxy - sumx*sumy) / sqrt((N*sumx2 - sumx*sumx)* (N*sumy2 - sumy*sumy) );
}

// Compute correlation between two double vectors of length N
double correlation(const LADSPA_Data* x, const LADSPA_Data* y, size_t N) {
    if (N == 0) return 0.0;

    double mean_x = 0.0, mean_y = 0.0;

    // First pass: compute means
    for (size_t i = 0; i < N; i++) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= N;
    mean_y /= N;

    double cov = 0.0;
    double stdx = 0;
    double stdy = 0;
    for (size_t i = 0; i < N; i++) {
      stdx += (x[i] - mean_x)*(x[i] - mean_x);
      stdy += (y[i] - mean_y)*(y[i] - mean_y);
      cov += (x[i] - mean_x) * (y[i] - mean_y);
    }
    return cov / (sqrt(stdx)*sqrt(stdy));
}

/////////////////////////////////////////////////////////////////////////////////////
// Calculates eigenvalues and eigenvectors of a 2x2 simmetric matrix. Matrix format:
//             [[ a  b ]
//              [ b  d ]]
// Returns eigenvalues eig1 and eig2 and eigenvectors v1 and v2
////////////////////////////////////////////////////////////////////////////////////
int eigen_2x2_symmetric(double a, double b, double d,double* eig1, double* eig2, double v1[2], double v2[2])
{
  // Compute trace and determinant
  double trace = a + d;
  double delta = a - d;
  double discrim = sqrt(delta*delta+4*b*b);

  // Eigenvalues
  *eig1 = 0.5 * (trace + discrim);
  *eig2 = 0.5 * (trace - discrim);

  // Compute eigenvectors
  if (fabs(b) > 1e-12) {
    // First eigenvector (eig1)
    v1[0] = 1;
    v1[1] = (*eig1 - a)/b;
    double norm1 = sqrt(v1[0]*v1[0] + v1[1]*v1[1]);
    v1[0] /= norm1;
    v1[1] /= norm1;

    // Second eigenvector (eig2)
    v2[0] = 1;
    v2[1] = (*eig2 - a)/b;
    double norm2 = sqrt(v2[0]*v2[0] + v2[1]*v2[1]);
    v2[0] /= norm2;
    v2[1] /= norm2;
  }
  else if (a >= d) {
    v1[0] = 1.0; v1[1] = 0.0;
    v2[0] = 0.0; v2[1] = 1.0;
  } else {
    v1[0] = 0.0; v1[1] = 1.0;
    v2[0] = 1.0; v2[1] = 0.0;
  }
  return 1; // Success
}

// Connect ports
static void connect_port(LADSPA_Handle instance, unsigned long port, LADSPA_Data *data) {
  ((CustomPlugin*)instance)->ports[port] = data;
}

// Instantiate
static LADSPA_Handle instantiate(const LADSPA_Descriptor *desc, unsigned long rate) {
  printf("Initiating " PLUGIN_NAME " (sample rate: %lu Hz)...\n", rate);
  CustomPlugin *plugin = calloc(1, sizeof(CustomPlugin));
  if (plugin)
    plugin->rate = rate;
  return plugin;
}

// Release every heap buffer allocated by run() and reset the pointers to NULL.
// Used both by cleanup() and by run()'s allocation-failure path. free(NULL) is a
// no-op, so buffers never allocated (e.g. icorrv.* in front mode) are safe.
static void free_plugin_buffers(CustomPlugin *plugin) {
  free(plugin->pca.mid_step);   plugin->pca.mid_step  = NULL;
  free(plugin->pca.side_step);  plugin->pca.side_step = NULL;
  free(plugin->pca.mid_left);   plugin->pca.mid_left  = NULL;
  free(plugin->pca.mid_right);  plugin->pca.mid_right = NULL;
  free(plugin->pca.side_left);  plugin->pca.side_left = NULL;
  free(plugin->pca.side_right); plugin->pca.side_right= NULL;

  free(plugin->covM.sum_xy_array); plugin->covM.sum_xy_array = NULL;
  free(plugin->covM.sum_x2_array); plugin->covM.sum_x2_array = NULL;
  free(plugin->covM.sum_y2_array); plugin->covM.sum_y2_array = NULL;
  free(plugin->covM.sum_x_array);  plugin->covM.sum_x_array  = NULL;
  free(plugin->covM.sum_y_array);  plugin->covM.sum_y_array  = NULL;

  free(plugin->icorrv.sum_xy_array); plugin->icorrv.sum_xy_array = NULL;
  free(plugin->icorrv.sum_x2_array); plugin->icorrv.sum_x2_array = NULL;
  free(plugin->icorrv.sum_y2_array); plugin->icorrv.sum_y2_array = NULL;
  free(plugin->icorrv.sum_x_array);  plugin->icorrv.sum_x_array  = NULL;
  free(plugin->icorrv.sum_y_array);  plugin->icorrv.sum_y_array  = NULL;
}

// Run
static void run(LADSPA_Handle instance, unsigned long sample_count) {
  CustomPlugin *plugin = (CustomPlugin*)instance;

  const LADSPA_Data *left_in = plugin->ports[PORT_INPUT1];
  const LADSPA_Data *right_in = plugin->ports[PORT_INPUT2];

  LADSPA_Data *left_out = plugin->ports[PORT_OUTPUT1];
  LADSPA_Data *right_out = plugin->ports[PORT_OUTPUT2];
  LADSPA_Data left_main;
  LADSPA_Data right_main;
  LADSPA_Data left_amb;
  LADSPA_Data right_amb;
  LADSPA_Data left_surr;
  LADSPA_Data right_surr;

  double cov_matrix[2][2];
  double eigvalues[2];
  double eigvectors[2][2];
  double mid_factor, side_factor;
  double sum_xy = 0;
  double sum_x2 = 0;
  double sum_y2 = 0;
  double sum_x = 0;
  double sum_y = 0;
  double c_sum_xy = 0;
  double c_sum_x2 = 0;
  double c_sum_y2 = 0;
  double c_sum_x = 0;
  double c_sum_y = 0;

#ifdef DEBUG
  LADSPA_Data *left_main_aux = calloc(sample_count, sizeof(LADSPA_Data));
  LADSPA_Data *right_main_aux = calloc(sample_count, sizeof(LADSPA_Data));
  LADSPA_Data *left_amb_aux = calloc(sample_count, sizeof(LADSPA_Data));
  LADSPA_Data *right_amb_aux = calloc(sample_count, sizeof(LADSPA_Data));
#endif

  // --- Dynamic control parameters -------------------------------------------
  // Re-read the control ports every block so the host can change them live, and
  // clamp each one to its declared range. mode (PORT_PARAM1) is a 0/1 switch;
  // the three gains (PARAM2..4) are dB values converted into linear factors via
  // FROM_DB -- positive dB amplifies, negative attenuates (same convention as
  // natambio's NAE gains).
  plugin->mode = (read_ctrl(plugin->ports[PORT_PARAM1], PARAM_DEFAULT,
                            PARAM_MODE_MIN, PARAM_MODE_MAX) >= 0.5) ? 1 : 0;
  plugin->gain_main = FROM_DB(
      read_ctrl(plugin->ports[PORT_PARAM2], PARAM_GAIN_DEFAULT, PARAM_GAIN_DB_MIN, PARAM_GAIN_DB_MAX));
  plugin->gain_amb = FROM_DB(
      read_ctrl(plugin->ports[PORT_PARAM3], PARAM_GAIN_DEFAULT, PARAM_GAIN_DB_MIN, PARAM_GAIN_DB_MAX));
  plugin->gain_main_surround = FROM_DB(
      read_ctrl(plugin->ports[PORT_PARAM4], PARAM_GAIN_DEFAULT, PARAM_GAIN_DB_MIN, PARAM_GAIN_DB_MAX));

  if(!plugin->pca.mid_step) {
    plugin->covsteps = COVSTEPS;
    plugin->beta = 1;
    plugin->icorr = 1;
    // pca.* are LADSPA_Data (float) buffers; calloc already zero-initialises.
    plugin->pca.mid_step  = calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));
    plugin->pca.side_step = calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));
    plugin->pca.mid_left  = calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));
    plugin->pca.mid_right = calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));
    plugin->pca.side_left = calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));
    plugin->pca.side_right= calloc(plugin->covsteps*sample_count, sizeof(LADSPA_Data));

    // Data for covariance calculation (calloc already zero-initialises)
    plugin->covM.sum_xy_array = (double*) calloc(plugin->covsteps, sizeof(double));
    plugin->covM.sum_x2_array = (double*) calloc(plugin->covsteps, sizeof(double));
    plugin->covM.sum_y2_array = (double*) calloc(plugin->covsteps, sizeof(double));
    plugin->covM.sum_x_array  = (double*) calloc(plugin->covsteps, sizeof(double));
    plugin->covM.sum_y_array  = (double*) calloc(plugin->covsteps, sizeof(double));

    // Inter-channel correlation buffers, used only in ambience mode. Allocated
    // unconditionally (not just when mode==1 at init) so PORT_PARAM1 can switch
    // mode live without reallocating on the realtime path. calloc zero-inits.
    plugin->icorrv.sum_xy_array = (double*) calloc(ICORRL, sizeof(double));
    plugin->icorrv.sum_x2_array = (double*) calloc(ICORRL, sizeof(double));
    plugin->icorrv.sum_y2_array = (double*) calloc(ICORRL, sizeof(double));
    plugin->icorrv.sum_x_array  = (double*) calloc(ICORRL, sizeof(double));
    plugin->icorrv.sum_y_array  = (double*) calloc(ICORRL, sizeof(double));

    // Abort safely if any allocation failed: keeps the realtime path from
    // dereferencing NULL. Partial buffers are released and reset to NULL so the
    // init block is retried on the next run() call; the output is silenced.
    if (!plugin->pca.mid_step  || !plugin->pca.side_step ||
        !plugin->pca.mid_left  || !plugin->pca.mid_right ||
        !plugin->pca.side_left || !plugin->pca.side_right ||
        !plugin->covM.sum_xy_array || !plugin->covM.sum_x2_array ||
        !plugin->covM.sum_y2_array || !plugin->covM.sum_x_array ||
        !plugin->covM.sum_y_array ||
        !plugin->icorrv.sum_xy_array || !plugin->icorrv.sum_x2_array ||
        !plugin->icorrv.sum_y2_array || !plugin->icorrv.sum_x_array ||
        !plugin->icorrv.sum_y_array) {
      free_plugin_buffers(plugin);
      if (left_out)  memset(left_out,  0, sample_count * sizeof(LADSPA_Data));
      if (right_out) memset(right_out, 0, sample_count * sizeof(LADSPA_Data));
#ifdef DEBUG
      free(left_main_aux);  free(right_main_aux);
      free(left_amb_aux);   free(right_amb_aux);
#endif
      return;
    }

#ifdef DEBUG
    // Ordered debug table header. One row is printed per processing block at the
    // end of run(); columns are aligned with the data format below.
    //   Sample   : block index               Time(s) : elapsed time
    //   LR_in    : input L/R correlation     Beta    : side-channel coeff. built from L/R correlation
    //   ML_MR    : mid_left / mid_right      SL_SR   : side_left / side_right
    //   ML_SL    : mid_left / side_left      MR_SR   : mid_right / side_right
    //   ML_SR    : mid_left / side_right     MR_SL   : mid_right / side_left
    //   Front_LR : front out L/R             Amb_LR  : ambience out L/R
    //   FL_AL    : frontL / ambL             FR_AR   : frontR / ambR
    //   FL_AR    : frontL / ambR             FR_AL   : frontR / ambL
    printf("\n");
    // Printed legend: explains the meaning of each column below. Every value
    // except Sample/Time/Beta is a Pearson correlation coefficient (range -1..1)
    // between the two signals named by the column.
    printf("Column legend (per processing block):\n");
    printf("  Sample   : block index                   Time(s) : elapsed time in seconds\n");
    printf("  LR_in    : input L / R correlation       beta    : side-channel coeff. built from the L/R (Pearson) correlation\n");
    printf("  ML_MR    : mid_left / mid_right          SL_SR   : side_left / side_right\n");
    printf("  ML_SL    : mid_left / side_left          MR_SR   : mid_right / side_right\n");
    printf("  ML_SR    : mid_left / side_right         MR_SL   : mid_right / side_left\n");
    printf("  Front_LR : front out L / R               Amb_LR  : ambience out L / R\n");
    printf("  ML_AL    : frontL / ambL                 FR_AR   : frontR / ambR\n");
    printf("  FL_AR    : frontL / ambR                 FR_AL   : frontR / ambL\n");
    printf("  (every column except Sample/Time/Beta is a Pearson correlation, -1..1)\n");
    printf("  between the two signals named by the column.\n");
    printf("\n");
    printf("%8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n",
           "Sample", "Time(s)", "LR_in", "Beta",
           "ML_MR", "SL_SR", "ML_SL", "MR_SR", "ML_SR", "MR_SL",
           "Front_LR", "Amb_LR", "FL_AL", "FR_AR", "FL_AR", "FR_AL");
    for (int k = 0; k < 8 + 15 * 11; k++) putchar('-');
    putchar('\n');
#endif
  }
  unsigned long norm_covsteps = plugin->covsteps + 1;

  plugin->covM.sum_xy_array[plugin->covsteps - 1] = 0;
  plugin->covM.sum_x2_array[plugin->covsteps - 1] = 0;
  plugin->covM.sum_y2_array[plugin->covsteps - 1] = 0;
  plugin->covM.sum_x_array[plugin->covsteps - 1] = 0;
  plugin->covM.sum_y_array[plugin->covsteps - 1] = 0;


  // mode = true for ambience extraction (for ambience stereo dipole)
  // mode = false for front main and ambience separation
  if(plugin->mode) {

    // ambience only
    plugin->icorrv.sum_xy_array[ICORRL - 1] = 0;
    plugin->icorrv.sum_x2_array[ICORRL - 1] = 0;
    plugin->icorrv.sum_y2_array[ICORRL - 1] = 0;
    plugin->icorrv.sum_x_array[ICORRL - 1] = 0;
    plugin->icorrv.sum_y_array[ICORRL - 1] = 0;

    // Correlation
    for (int i = 0; i < sample_count; i++) {
      plugin->icorrv.sum_xy_array[ICORRL - 1] += left_in[i] * right_in[i];
      plugin->icorrv.sum_x2_array[ICORRL - 1] += left_in[i] * left_in[i];
      plugin->icorrv.sum_y2_array[ICORRL - 1] += right_in[i] * right_in[i];
      plugin->icorrv.sum_x_array[ICORRL - 1] += left_in[i];
      plugin->icorrv.sum_y_array[ICORRL - 1] += right_in[i];
    }
    for(int i = 0; i <  ICORRL; i++) {
      c_sum_xy += plugin->icorrv.sum_xy_array[i];
      c_sum_x2 += plugin->icorrv.sum_x2_array[i];
      c_sum_y2 += plugin->icorrv.sum_y2_array[i];
      c_sum_x += plugin->icorrv.sum_x_array[i];
      c_sum_y += plugin->icorrv.sum_y_array[i];
    }
    // Control for potential computing errors
    if(ICORRL * sample_count * c_sum_x2 <= c_sum_x * c_sum_x || ICORRL * sample_count * c_sum_y2 <= c_sum_y * c_sum_y)  {
      plugin->icorr = 1;
    }
    else
      plugin->icorr = fabs(ICORRL * sample_count * c_sum_xy - c_sum_x*c_sum_y) / sqrt((ICORRL * sample_count * c_sum_x2 - c_sum_x*c_sum_x)* (ICORRL * sample_count * c_sum_y2 - c_sum_y * c_sum_y));
    plugin->beta = 0.55 + plugin->icorr*0.45;
  }
  else {
    plugin->beta = 1.0;
  }
  for (unsigned long i = 0, j = (plugin->covsteps-1)*sample_count; i < sample_count; i++, j++) {
    plugin->pca.mid_step[j] = left_in[i] + right_in[i];
    plugin->pca.side_step[j] = plugin->beta*(left_in[i] - right_in[i]);

    // Covariances
    plugin->covM.sum_xy_array[plugin->covsteps - 1] += plugin->pca.mid_step[j] * plugin->pca.side_step[j];
    plugin->covM.sum_x2_array[plugin->covsteps - 1] += plugin->pca.mid_step[j] * plugin->pca.mid_step[j];
    plugin->covM.sum_y2_array[plugin->covsteps - 1] += plugin->pca.side_step[j] * plugin->pca.side_step[j];
    plugin->covM.sum_x_array[plugin->covsteps - 1] += plugin->pca.mid_step[j];
    plugin->covM.sum_y_array[plugin->covsteps - 1] += plugin->pca.side_step[j];
  }

  for(unsigned long i = 0; i < plugin->covsteps; i++) {
    // Covariance matrix calculation
    sum_xy += plugin->covM.sum_xy_array[i];
    sum_x2 += plugin->covM.sum_x2_array[i];
    sum_y2 += plugin->covM.sum_y2_array[i];
    sum_x += plugin->covM.sum_x_array[i];
    sum_y += plugin->covM.sum_y_array[i];
  }

  unsigned long N = (plugin->covsteps)*sample_count;

  cov_matrix[0][0] = (sum_x2 - 2*sum_x*sum_x/N + sum_x*sum_x/N)/(N - 1);
  cov_matrix[1][1] = (sum_y2 - 2*sum_y*sum_y/N + sum_y*sum_y/N)/(N -1);
  cov_matrix[1][0] = (sum_xy - sum_y*sum_x/N)/(N -1);

  // Eigenvalues and eigenvectors
  eigen_2x2_symmetric(cov_matrix[0][0], cov_matrix[1][0], cov_matrix[1][1], &eigvalues[0], &eigvalues[1], eigvectors[0], eigvectors[1]);

  //Components

  // Output
  if(plugin->mode) {
    // Ambience
    for(unsigned long i = 0; i < plugin->covsteps * sample_count; i ++) {
#ifdef DEBUG
      mid_factor =  eigvectors[0][0] * plugin->pca.mid_step[i] + eigvectors[0][1] * plugin->pca.side_step[i];
      plugin->pca.mid_left[i] += mid_factor * eigvectors[0][0]; // = mid_left
      plugin->pca.mid_right[i] += mid_factor * eigvectors[0][1]; // = mid_right
#endif
      side_factor = eigvectors[1][0] * plugin->pca.mid_step[i] + eigvectors[1][1] * plugin->pca.side_step[i];
      plugin->pca.side_left[i] += side_factor * eigvectors[1][0]; // = side_left
      plugin->pca.side_right[i] += side_factor * eigvectors[1][1]; // = side_right
    }
    for(unsigned long  i = 0; i < sample_count; i++) {
      left_surr = (plugin->pca.side_left[i] + plugin->pca.side_right[i])/(norm_covsteps);
      right_surr = (plugin->pca.side_left[i] - plugin->pca.side_right[i])/(norm_covsteps);
      left_out[i]  = plugin->gain_main_surround*left_surr; // right_ambience
      right_out[i] = plugin->gain_main_surround*right_surr; // left_ambience

#ifdef DEBUG
      left_main_aux[i] = (plugin->pca.mid_left[i] + plugin->pca.mid_right[i])/(norm_covsteps);
      right_main_aux[i] = (plugin->pca.mid_left[i] - plugin->pca.mid_right[i])/(norm_covsteps);
      left_amb_aux[i] = left_surr;
      right_amb_aux[i] = right_surr;
#endif

    }
  } else {
    // Main /Front dipole calculation
    for(unsigned long i = 0; i < plugin->covsteps * sample_count; i ++) {
      mid_factor =  eigvectors[0][0] * plugin->pca.mid_step[i] + eigvectors[0][1] * plugin->pca.side_step[i];
      side_factor = eigvectors[1][0] * plugin->pca.mid_step[i] + eigvectors[1][1] * plugin->pca.side_step[i];
      plugin->pca.mid_left[i] += mid_factor * eigvectors[0][0]; // = mid_left
      plugin->pca.mid_right[i] += mid_factor * eigvectors[0][1]; // = mid_right
      plugin->pca.side_left[i] += side_factor * eigvectors[1][0]; // = side_left
      plugin->pca.side_right[i] += side_factor * eigvectors[1][1]; // = side_right
    }
    for(unsigned long  i = 0; i < sample_count; i++) {
      left_main = (plugin->pca.mid_left[i] + plugin->pca.mid_right[i])/(norm_covsteps);
      right_main = (plugin->pca.mid_left[i] - plugin->pca.mid_right[i])/(norm_covsteps);
      left_amb = (plugin->pca.side_left[i] + plugin->pca.side_right[i])/(norm_covsteps);
      right_amb = (plugin->pca.side_left[i] - plugin->pca.side_right[i])/(norm_covsteps);
      left_out[i]  = plugin->gain_main*left_main + plugin->gain_amb*left_amb;
      right_out[i] = plugin->gain_main*right_main + plugin->gain_amb*right_amb;

#ifdef DEBUG
      left_main_aux[i] = left_main;
      right_main_aux[i] = right_main;
      left_amb_aux[i] = left_amb;
      right_amb_aux[i] = right_amb;
#endif
    }
  }

#ifdef DEBUG
  // One aligned row per processing block (columns match the header above).
  double lr_in_corr     = correlation(left_in, right_in, sample_count);
  double mid_corr       = correlation(plugin->pca.mid_left,  plugin->pca.mid_right,  sample_count);
  double side_corr      = correlation(plugin->pca.side_left, plugin->pca.side_right, sample_count);
  double ms_l_corr      = correlation(plugin->pca.mid_left,  plugin->pca.side_left,  sample_count);
  double ms_r_corr      = correlation(plugin->pca.mid_right, plugin->pca.side_right, sample_count);
  double mlar_corr      = correlation(plugin->pca.mid_left,  plugin->pca.side_right, sample_count);
  double mral_corr      = correlation(plugin->pca.mid_right, plugin->pca.side_left,  sample_count);
  double main_corr      = correlation(left_main_aux,  right_main_aux, sample_count);
  double amb_corr       = correlation(left_amb_aux,   right_amb_aux,  sample_count);
  double cross_left_corr  = correlation(left_main_aux,  left_amb_aux,  sample_count);
  double cross_right_corr = correlation(right_main_aux, right_amb_aux, sample_count);
  double cross_lr_corr    = correlation(left_main_aux,  right_amb_aux, sample_count);
  double cross_rl_corr    = correlation(right_main_aux, left_amb_aux,  sample_count);
  double time_s = plugin->rate ?
    (double)(plugin->step_count * sample_count) / (double)plugin->rate : 0.0;

  // Skip the warm-up blocks: until the covariance window of covsteps buffers is
  // filled (step_count = covsteps - 1) the correlations are nan, so they are not
  // printed.
  if (plugin->step_count >= (unsigned long)(plugin->covsteps - 1))
    printf("%8lu %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f\n",
           plugin->step_count, time_s, lr_in_corr, plugin->beta,
           mid_corr, side_corr, ms_l_corr, ms_r_corr, mlar_corr, mral_corr,
           main_corr, amb_corr, cross_left_corr, cross_right_corr, cross_lr_corr, cross_rl_corr);
  plugin->step_count += 1;

  free(left_main_aux);
  free(right_main_aux);
  free(left_amb_aux);
  free(right_amb_aux);
#endif

  for(int i = 0, j = sample_count; i < sample_count *(plugin->covsteps - 1); i++, j++) {
    plugin->pca.mid_left[i] = plugin->pca.mid_left[j];
    plugin->pca.mid_right[i] = plugin->pca.mid_right[j];
    plugin->pca.side_left[i] = plugin->pca.side_left[j];
    plugin->pca.side_right[i] = plugin->pca.side_right[j];
    plugin->pca.mid_step[i] = plugin->pca.mid_step[j];
    plugin->pca.side_step[i] = plugin->pca.side_step[j];
  }

  for(int i = sample_count *(plugin->covsteps - 1); i < sample_count * plugin->covsteps; i++) {
    plugin->pca.mid_left[i] = 0;
    plugin->pca.mid_right[i] = 0;
    plugin->pca.side_left[i] = 0;
    plugin->pca.side_right[i] = 0;
  }

  for(unsigned long i = 0; i < plugin->covsteps - 1; i++) {
    plugin->covM.sum_xy_array[i] = plugin->covM.sum_xy_array[i + 1];
    plugin->covM.sum_x2_array[i] = plugin->covM.sum_x2_array[i + 1];
    plugin->covM.sum_y2_array[i] = plugin->covM.sum_y2_array[i + 1];
    plugin->covM.sum_x_array[i] = plugin->covM.sum_x_array[i + 1];
    plugin->covM.sum_y_array[i] = plugin->covM.sum_y_array[i + 1];
  }
  if(plugin->mode) {
    for(int i = 0; i < ICORRL - 1; i++) {
      plugin->icorrv.sum_xy_array[i] = plugin->icorrv.sum_xy_array[i + 1];
      plugin->icorrv.sum_x2_array[i] = plugin->icorrv.sum_x2_array[i + 1];
      plugin->icorrv.sum_y2_array[i] = plugin->icorrv.sum_y2_array[i + 1];
      plugin->icorrv.sum_x_array[i] = plugin->icorrv.sum_x_array[i + 1];
      plugin->icorrv.sum_y_array[i] = plugin->icorrv.sum_y_array[i + 1];
    }
  }
}

// Cleanup
static void cleanup(LADSPA_Handle instance) {
    CustomPlugin *plugin = (CustomPlugin*)instance;
    if (!plugin) return;
    free_plugin_buffers(plugin);   // free the run()-allocated buffers first
    free(plugin);                  // then the CustomPlugin container
}

// Plugin Descriptor
static LADSPA_Descriptor *descriptor = NULL;
#  define ON_LOAD_ROUTINE   static void __attribute__ ((constructor)) init()
#  define ON_UNLOAD_ROUTINE static void __attribute__ ((destructor))  fini()

////////////////////////////////////////////////////////////////////////////////////////////////////
// Initializes LADSPA_Descriptor structure. This plug-in has only one signal processing block so
//it has only one LADSPA_Description structure
///////////////////////////////////////////////////////////////////////////////////////////////////
ON_LOAD_ROUTINE {
    descriptor = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
    if (!descriptor) return;   // ladspa_descriptor() will then report no plugin

    descriptor->ImplementationData = NULL;
    descriptor->UniqueID = PLUGIN_ID;
    descriptor->Label = PLUGIN_LABEL;
    descriptor->Properties = 0;
    descriptor->Name = PLUGIN_NAME;
    descriptor->Maker = PLUGIN_MAKER;
    descriptor->Copyright = PLUGIN_COPYRIGHT;
    descriptor->PortCount = NUM_PORTS;

    LADSPA_PortDescriptor* port_desc = calloc(NUM_PORTS, sizeof(LADSPA_PortDescriptor));
    LADSPA_PortRangeHint *hints = calloc(NUM_PORTS, sizeof(LADSPA_PortRangeHint));
    const char **port_names = calloc(NUM_PORTS, sizeof(char*));

    if (!port_desc || !hints || !port_names) {
      free(port_desc);
      free(hints);
      free(port_names);
      free(descriptor);
      descriptor = NULL;
      return;
    }

    descriptor->PortDescriptors = port_desc;
    descriptor->PortNames = port_names;
    descriptor->PortRangeHints = hints;

    // Inputs
    //PORT_INPUT1
    port_names[PORT_INPUT1] = "Left";
    port_desc[PORT_INPUT1] = LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO;
    //PORT_INPUT2
    port_names[PORT_INPUT2] = "Right";
    port_desc[PORT_INPUT2] = LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO;

    // Outputs
    //PORT_OUTPUT1
    port_names[PORT_OUTPUT1] = "Left";
    port_desc[PORT_OUTPUT1] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    //PORT_OUTPUT2
    port_names[PORT_OUTPUT2] = "Right";
    port_desc[PORT_OUTPUT2] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;

    // Control parameters
    //PORT_PARAM1
    port_names[PORT_PARAM1]	= "Plugin Mode (0 - Alpha / front separation / 1 - Beta / ambience)";
    port_desc[PORT_PARAM1]	= LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;
    hints[PORT_PARAM1].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_1;
    hints[PORT_PARAM1].LowerBound = PARAM_MODE_MIN;
    hints[PORT_PARAM1].UpperBound = PARAM_MODE_MAX;
    //PORT_PARAM2
    port_names[PORT_PARAM2]	= "Alpha Front gain (dB)";
    port_desc[PORT_PARAM2]	= LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;
    hints[PORT_PARAM2].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0;
    hints[PORT_PARAM2].LowerBound = PARAM_GAIN_DB_MIN;
    hints[PORT_PARAM2].UpperBound = PARAM_GAIN_DB_MAX;
    //PORT_PARAM3
    port_names[PORT_PARAM3]	= "Alpha Ambience Gain (dB)";
    port_desc[PORT_PARAM3]	= LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;
    hints[PORT_PARAM3].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0;
    hints[PORT_PARAM3].LowerBound = PARAM_GAIN_DB_MIN;
    hints[PORT_PARAM3].UpperBound = PARAM_GAIN_DB_MAX;
    //PORT_PARAM4
    port_names[PORT_PARAM4]	= "Beta Ambience Gain (dB)";
    port_desc[PORT_PARAM4]	= LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;
    hints[PORT_PARAM4].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0;
    hints[PORT_PARAM4].LowerBound = PARAM_GAIN_DB_MIN;
    hints[PORT_PARAM4].UpperBound = PARAM_GAIN_DB_MAX;

    descriptor->instantiate = instantiate;
    descriptor->connect_port = connect_port;
    descriptor->activate = NULL;
    descriptor->run = run;
    descriptor->run_adding = NULL;
    descriptor->set_run_adding_gain = NULL;
    descriptor->deactivate = NULL;
    descriptor->cleanup = cleanup;
}

ON_UNLOAD_ROUTINE {
  if (descriptor) {
    free((void*)descriptor->PortDescriptors);
    free((void*)descriptor->PortNames);
    free((void*)descriptor->PortRangeHints);
    free(descriptor);
  }
}

const LADSPA_Descriptor* ladspa_descriptor(unsigned long index) {
    return (index == 0) ? descriptor : NULL;
}
