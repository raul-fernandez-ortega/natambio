/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#include "nae.hpp"

#define NAE_BETA_1 0.55
#define NAE_BETA_2 0.45


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

static void* process_front(void *n_nae)
{
  NAE *nae = (NAE*) n_nae;
  nae->thr_process();
  return (void*) nae;
}

static void* process_ambient(void *n_nae)
{
  NAE *nae = (NAE*) n_nae;
  nae->thr_process();
  return (void*) nae;
}

NAE::NAE(string n_name, int n_mode)
{
  name = n_name;
  quiet = false;
  mode = n_mode;
  prio = 0;
  left_name_in = "";
  left_name_out = "";
  right_name_in = "";
  right_name_out = "";
  c1_left_name_out = "";
  c2_left_name_out = "";
  c1_right_name_out = "";
  c2_right_name_out = "";
  pan_scale = 0;
  pan_a = 1.0;
  pan_b = 0.0;
}

NAE::~NAE(void)
{
  run = false;
  sem_post(&semaphore); 
  pthread_join(t_proc, NULL);
  free(left_in);
  free(right_in);
  free(left_out);
  free(right_out);
  free(c1_left_out);
  free(c1_right_out);
  free(c2_left_out);
  free(c2_right_out);
  free(pca.mid_step);
  free(pca.side_step);
  free(pca.c1_mid);
  free(pca.c2_mid);
  free(pca.c1_side);
  free(pca.c2_side);
  free(covM.sum_xy_array);
  free(covM.sum_x2_array);
  free(covM.sum_y2_array);
  free(covM.sum_x_array);
  free(covM.sum_y_array);
  if(mode) {
    free(icorrv.sum_xy_array);
    free(icorrv.sum_x2_array);
    free(icorrv.sum_y2_array);
    free(icorrv.sum_x_array);
    free(icorrv.sum_y_array);
  }
  sem_destroy(&semaphore);
  pthread_mutex_destroy(&mutex);
}

bool NAE::setC1Gain(double gain)
{
  gain_c1 = gain;
  return true;
}

bool NAE::setC2Gain(double gain)
{
  gain_c2 = gain;
  return true;
}

bool NAE::setC2RearGain(double gain)
{
  gain_c2_rear = gain;
  return true;
}

void NAE::setPanScale(double n_scale)
{
  pan_scale = n_scale;
  // Width of the input pair (<pan_scale>), as a plain constant matrix on L/R,
  // applied before anything else looks at the signal. Written as an angle so
  // the two weights stay power complementary, wm^2 + ws^2 = 2:
  //
  //     phi = 45deg * (1 - pan_scale),  wm = sqrt(2)*cos(phi), ws = sqrt(2)*sin(phi)
  //
  // which puts +1 at mono (the side weight vanishing), 0 at the signal
  // untouched, and -1 at the two channels in opposite polarity (the mid weight
  // vanishing). A width control cannot be energy preserving in the strict
  // sense -- [[a,b],[b,a]] is orthogonal only for b = 0 or a = 0 -- so power
  // complementary is as close as it gets: exact whenever mid and side carry
  // the same power, and bounded and smooth otherwise.
  double phi = (M_PI/4.0)*(1.0 - pan_scale);
  double wm = M_SQRT2*cos(phi);
  double ws = M_SQRT2*sin(phi);
  pan_a = 0.5*(wm + ws);
  pan_b = 0.5*(wm - ws);
}

void NAE::setSampleCount(int n_sample_count)
{
  sample_count = n_sample_count;
  left_in = (float*) calloc(sample_count, sizeof(float));
  right_in = (float*) calloc(sample_count, sizeof(float));
  memset(left_in, 0, sample_count);
  memset(right_in, 0, sample_count);

  left_out = (float*) calloc(sample_count, sizeof(float));
  right_out = (float*) calloc(sample_count, sizeof(float));
  memset(left_out, 0, sample_count);
  memset(right_out, 0, sample_count);
  
  c1_left_out = (float*) calloc(sample_count, sizeof(float));
  c1_right_out = (float*) calloc(sample_count, sizeof(float));
  memset(c1_left_out, 0, sample_count);
  memset(c1_right_out, 0, sample_count);
  
  c2_left_out = (float*) calloc(sample_count, sizeof(float));
  c2_right_out = (float*) calloc(sample_count, sizeof(float));
  memset(c2_left_out, 0, sample_count);
  memset(c2_right_out, 0, sample_count);
}

void NAE::setCovStepsLength(int n_covsteps)
{
  covsteps = n_covsteps;
}


void NAE::setChannelIn(enum side n_side, string n_channel_in)
{
  if(n_side == LEFT) {
    left_name_in = n_channel_in;
  }
  else {
    right_name_in = n_channel_in;
  } 
}

void NAE::setChannelOut(enum side n_side, string n_channel_out)
{
  if(n_side == LEFT)
    left_name_out = n_channel_out;
  else if (n_side == RIGHT)
    right_name_out = n_channel_out;
  else if (n_side == C1_LEFT)
    c1_left_name_out = n_channel_out;
  else if (n_side == C1_RIGHT)
    c1_right_name_out = n_channel_out;
  else if (n_side == C2_LEFT)
    c2_left_name_out = n_channel_out;
  else
    c2_right_name_out = n_channel_out;
}

string NAE::getChannelIn(enum side n_side)
{
  if(n_side == LEFT)
    return left_name_in;
  else 
    return right_name_in;
}

string NAE::getChannelOut(enum side n_side)
{
  if(n_side == LEFT) 
    return left_name_out;
  else if(n_side == RIGHT) 
    return right_name_out;
  else if(n_side == C1_LEFT) 
    return c1_left_name_out;
  else if(n_side == C1_RIGHT) 
    return c1_right_name_out;
  else if(n_side == C2_LEFT) 
    return c2_left_name_out;
  else 
    return c2_right_name_out;
}

void NAE::fillInputBuffer(enum side n_side, const float *n_input)
{
  if(n_side == LEFT) {
#ifdef RTDEBUG
    std::cout << "NAE: copying buffer to left input " << std::endl;
#endif
    memcpy(left_in, n_input, sample_count*sizeof(float));
#ifdef RTDEBUG
    std::cout << "l_in:" << left_in[0] << std::endl;
#endif
  }
  else {
#ifdef RTDEBUG
    std::cout << "NAE: copying buffer to right input " << std::endl;
#endif
    memcpy(right_in, n_input, sample_count*sizeof(float));
#ifdef RTDEBUG
    std::cout << "r_in:" << left_in[0] << std::endl;
#endif
  }
}

void NAE::fillOutputBuffer(enum side n_side, float* n_output)
{
  pthread_mutex_lock(&mutex);
  if(n_side == LEFT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += left_out[i];
#ifdef RTDEBUG
    std::cout << "l_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == RIGHT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += right_out[i];
#ifdef RTDEBUG
    std::cout << "r_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == C1_LEFT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += c1_left_out[i];
#ifdef RTDEBUG
    std::cout << "l_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == C1_RIGHT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += c1_right_out[i];
#ifdef RTDEBUG
    std::cout << "r_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == C2_LEFT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += c2_left_out[i];
#ifdef RTDEBUG
    std::cout << "l_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == C2_RIGHT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += c2_right_out[i];
#ifdef RTDEBUG
    std::cout << "r_out:" << n_output[0] << std::endl;
#endif
  }  
  pthread_mutex_unlock(&mutex);
}

void NAE::load(int abspri, int policy)
{

  int min, max;

#ifdef RTDEBUG
  std::cout << "NAE: initial loading " << name << std::endl;
#endif

  side_weight = 1;
  icorr = 1;

  pca.mid_step = (double*) calloc(covsteps*sample_count, sizeof(double));
  pca.side_step = (double*) calloc(covsteps*sample_count, sizeof(double));
  pca.c1_mid = (double*) calloc(covsteps*sample_count, sizeof(double));
  pca.c1_side = (double*) calloc(covsteps*sample_count, sizeof(double));
  pca.c2_mid = (double*) calloc(covsteps*sample_count, sizeof(double));
  pca.c2_side = (double*) calloc(covsteps*sample_count, sizeof(double));

  // Data for covariance calculation
  covM.sum_xy_array = (double*) calloc(covsteps, sizeof(double));
  covM.sum_x2_array =  (double*) calloc(covsteps, sizeof(double));
  covM.sum_y2_array =  (double*) calloc(covsteps, sizeof(double));
  covM.sum_x_array = (double*) calloc(covsteps, sizeof(double));
  covM.sum_y_array =  (double*) calloc(covsteps, sizeof(double));

  memset(covM.sum_xy_array, 0, (covsteps)*sizeof(double));
  memset(covM.sum_x2_array, 0, (covsteps)*sizeof(double));
  memset(covM.sum_y2_array, 0, (covsteps)*sizeof(double));
  memset(covM.sum_x_array, 0, (covsteps)*sizeof(double));
  memset(covM.sum_y_array, 0, (covsteps)*sizeof(double));

  if(mode) {
    // Data for correlation calculation
    icorrv.sum_xy_array = (double*) calloc(ICORRL, sizeof(double));
    icorrv.sum_x2_array =  (double*) calloc(ICORRL, sizeof(double));
    icorrv.sum_y2_array =  (double*) calloc(ICORRL, sizeof(double));
    icorrv.sum_x_array = (double*) calloc(ICORRL, sizeof(double));
    icorrv.sum_y_array =  (double*) calloc(ICORRL, sizeof(double));
  }
  
  run = true;
  
  // run thread
  t_proc = 0;
  min = sched_get_priority_min(policy);
  max = sched_get_priority_max(policy);
  abspri += prio;
  if (abspri > max) abspri = max;
  if (abspri < min) abspri = min;
  parm.sched_priority = abspri;
  sem_init(&semaphore, 0, 0);
  pthread_attr_init(&attr);
  pthread_attr_setschedpolicy(&attr, policy);
  pthread_attr_setschedparam(&attr, &parm);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setstacksize(&attr, 0x10000);
  if(mode)
    pthread_create(&t_proc, &attr, process_ambient, (void *)this);
  else
    pthread_create(&t_proc, &attr, process_front, (void *)this);
  pthread_attr_destroy(&attr);
  pthread_mutex_init(&mutex, NULL);
}

void NAE::signal(void)
{
#ifdef RTDEBUG
  std::cout << "NAE: signaling " << name << std::endl;
#endif
  sem_post(&semaphore); 
}

void NAE::thr_process(void)
{ 
  double cov_matrix[2][2];
  double eigvalues[2];
  double eigvectors[2][2];
  double c1_factor, c2_factor;
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
  int norm_covsteps = covsteps + 1;
  double c1_left;
  double c1_right;
  double c2_left;
  double c2_right;
  int N = (covsteps)*sample_count;

  if(!quiet) {
    std::cout << "NAE: running thread " << this->name << std::endl;
    }  

  while(run) {
   
    // wait to semaphore signal
    sem_wait(&semaphore);

#ifdef RTDEBUG
    std::cout << "NAE: processing " << name << std::endl;
#endif

    // process input
    covM.sum_xy_array[covsteps - 1] = 0;
    covM.sum_x2_array[covsteps - 1] = 0;
    covM.sum_y2_array[covsteps - 1] = 0;
    covM.sum_x_array[covsteps - 1] = 0;
    covM.sum_y_array[covsteps - 1] = 0;

    if(mode) {
      // beta mode only
      icorrv.sum_xy_array[ICORRL - 1] = 0;
      icorrv.sum_x2_array[ICORRL - 1] = 0;
      icorrv.sum_y2_array[ICORRL - 1] = 0;
      icorrv.sum_x_array[ICORRL - 1] = 0;
      icorrv.sum_y_array[ICORRL - 1] = 0;

      // Correlation, of the pair as the width control leaves it: <pan_scale>
      // acts before anything else looks at the signal, so the weighting this
      // correlation drives follows the width too.
      for (int i = 0; i < sample_count; i++) {
        double l = pan_a*left_in[i] + pan_b*right_in[i];
        double r = pan_b*left_in[i] + pan_a*right_in[i];
        icorrv.sum_xy_array[ICORRL - 1] += l * r;
        icorrv.sum_x2_array[ICORRL - 1] += l * l;
        icorrv.sum_y2_array[ICORRL - 1] += r * r;
        icorrv.sum_x_array[ICORRL - 1] += l;
        icorrv.sum_y_array[ICORRL - 1] += r;
      }
      for(int i = 0; i < ICORRL; i++) {
        c_sum_xy += icorrv.sum_xy_array[i];
        c_sum_x2 += icorrv.sum_x2_array[i];
        c_sum_y2 += icorrv.sum_y2_array[i];
        c_sum_x += icorrv.sum_x_array[i];
        c_sum_y += icorrv.sum_y_array[i];
      }
      if(ICORRL*sample_count*c_sum_x2 <= c_sum_x*c_sum_x || ICORRL*sample_count*c_sum_y2 <= c_sum_y*c_sum_y)
        icorr = 1;
      else 
        icorr = fabs(ICORRL*sample_count*c_sum_xy - c_sum_x*c_sum_y) / sqrt((ICORRL*sample_count*c_sum_x2 - c_sum_x*c_sum_x)* (ICORRL*sample_count*c_sum_y2 - c_sum_y*c_sum_y));
      side_weight = NAE_BETA_1 + icorr * NAE_BETA_2;
    }
    else {
      side_weight = 1.0;
    }
    
    for (int i = 0, j = (covsteps - 1) * sample_count; i < sample_count; i++, j++) {
      // Width first, then the decomposition works on the pair as it leaves it.
      double l = pan_a*left_in[i] + pan_b*right_in[i];
      double r = pan_b*left_in[i] + pan_a*right_in[i];
      pca.mid_step[j] = l + r;
      pca.side_step[j] = side_weight*(l - r);
      // Covariances
      covM.sum_xy_array[covsteps - 1] += pca.mid_step[j] * pca.side_step[j];
      covM.sum_x2_array[covsteps - 1] += pca.mid_step[j] * pca.mid_step[j];
      covM.sum_y2_array[covsteps - 1] += pca.side_step[j] * pca.side_step[j];
      covM.sum_x_array[covsteps - 1] += pca.mid_step[j];
      covM.sum_y_array[covsteps - 1] += pca.side_step[j];
    }
    sum_xy = 0;
    sum_x2 = 0;
    sum_y2 = 0;
    sum_x = 0;
    sum_y = 0;
    for(int i = 0; i < covsteps; i++) {
      // Covariance matrix calculation
      sum_xy += covM.sum_xy_array[i];
      sum_x2 += covM.sum_x2_array[i];
      sum_y2 += covM.sum_y2_array[i];
      sum_x += covM.sum_x_array[i];
      sum_y += covM.sum_y_array[i];
    }
    
    cov_matrix[0][0] = (sum_x2 - 2*sum_x*sum_x/N + sum_x*sum_x/N)/(N - 1);
    cov_matrix[1][1] = (sum_y2 - 2*sum_y*sum_y/N + sum_y*sum_y/N)/(N -1);
    cov_matrix[1][0] = (sum_xy - sum_y*sum_x/N)/(N -1);
    
    // Eigenvalues and eigenvectors
    eigen_2x2_symmetric(cov_matrix[0][0], cov_matrix[1][0], cov_matrix[1][1], &eigvalues[0], &eigvalues[1], eigvectors[0], eigvectors[1]);
    
    //Components
    pthread_mutex_lock(&mutex);
    
    // Output: ambient
    if(mode) {
      // Beta ambient calculation
      for(int i = 0; i < covsteps * sample_count; i ++) {
        c2_factor = eigvectors[1][0] * pca.mid_step[i] + eigvectors[1][1] * pca.side_step[i];
        pca.c2_mid[i] += c2_factor * eigvectors[1][0];
        pca.c2_side[i] += c2_factor * eigvectors[1][1];
      }
      for(int  i = 0; i < sample_count; i++) {
        c2_left = (pca.c2_mid[i] + pca.c2_side[i])/(norm_covsteps);
        c2_right = (pca.c2_mid[i] - pca.c2_side[i])/(norm_covsteps);
        left_out[i]  = gain_c2_rear*c2_left;
        right_out[i] = gain_c2_rear*c2_right;
        c2_left_out[i] = left_out[i];
        c2_right_out[i] = right_out[i];
      }
    } else {
      // Alpha / Front main and ambient calculation
      for(int i = 0; i < covsteps * sample_count; i ++) {
        c1_factor =  eigvectors[0][0] * pca.mid_step[i] + eigvectors[0][1] * pca.side_step[i];
        c2_factor = eigvectors[1][0] * pca.mid_step[i] + eigvectors[1][1] * pca.side_step[i];
        pca.c1_mid[i] += c1_factor * eigvectors[0][0];
        pca.c1_side[i] += c1_factor * eigvectors[0][1];
        pca.c2_mid[i] += c2_factor * eigvectors[1][0];
        pca.c2_side[i] += c2_factor * eigvectors[1][1];
      }
      for(int  i = 0; i < sample_count; i++) {
        c1_left = (pca.c1_mid[i] + pca.c1_side[i])/(norm_covsteps);
        c1_right = (pca.c1_mid[i] - pca.c1_side[i])/(norm_covsteps);
        c2_left = (pca.c2_mid[i] + pca.c2_side[i])/(norm_covsteps);
        c2_right = (pca.c2_mid[i] - pca.c2_side[i])/(norm_covsteps);
        left_out[i]  = gain_c1*c1_left + gain_c2*c2_left;
        right_out[i] = gain_c1*c1_right + gain_c2*c2_right;
        c1_left_out[i] = gain_c1*c1_left;
        c1_right_out[i] = gain_c1*c1_right;
        c2_left_out[i] = gain_c2*c2_left;
        c2_right_out[i] = gain_c2*c2_right;
      }
    }
    
    for(int i = 0, j = sample_count; i < sample_count *(covsteps - 1); i++, j++) {
      pca.c1_mid[i] = pca.c1_mid[j];
      pca.c1_side[i] = pca.c1_side[j];
      pca.c2_mid[i] = pca.c2_mid[j];
      pca.c2_side[i] = pca.c2_side[j];
      pca.mid_step[i] = pca.mid_step[j];
      pca.side_step[i] = pca.side_step[j];
    }
    
    for(int i = sample_count *(covsteps - 1); i < sample_count * covsteps; i++) {
      pca.c1_mid[i] = 0;
      pca.c1_side[i] = 0;
      pca.c2_mid[i] = 0;
      pca.c2_side[i] = 0;
    }
    
    for(int i = 0; i < covsteps - 1; i++) {
      covM.sum_xy_array[i] = covM.sum_xy_array[i + 1];
      covM.sum_x2_array[i] = covM.sum_x2_array[i + 1];
      covM.sum_y2_array[i] = covM.sum_y2_array[i + 1];
      covM.sum_x_array[i] = covM.sum_x_array[i + 1];
      covM.sum_y_array[i] = covM.sum_y_array[i + 1];
    }
    if(mode) {
      // ambient mode only
      for(int i = 0; i < ICORRL - 1; i++) {
      icorrv.sum_xy_array[i] = icorrv.sum_xy_array[i + 1];
      icorrv.sum_x2_array[i] = icorrv.sum_x2_array[i + 1];
      icorrv.sum_y2_array[i] = icorrv.sum_y2_array[i + 1];
      icorrv.sum_x_array[i] = icorrv.sum_x_array[i + 1];
      icorrv.sum_y_array[i] = icorrv.sum_y_array[i + 1];
      }
      c_sum_xy = 0;
      c_sum_x2 = 0;
      c_sum_y2 = 0;
      c_sum_x = 0;
      c_sum_y = 0;
    }
    pthread_mutex_unlock(&mutex);
  }
  if(!quiet) {
    std::cout << "NAE: stopping thread " << this->name << std::endl;
  } 
}




