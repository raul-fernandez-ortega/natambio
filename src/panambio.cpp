/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#include "panambio.hpp"


// Compute covariance between two double vectors of length N
double covariance(const float* x, const float* y, int N)
{
  if (N == 0) return 0.0;
  
  double mean_x = 0.0, mean_y = 0.0;
  
  // First pass: compute means
  for (int i = 0; i < N; i++) {
    mean_x += x[i];
    mean_y += y[i];
  }
  mean_x /= N;
  mean_y /= N;
  //printf("Mean:%13.7e %13.7e\n",mean_x,mean_y);
  // Second pass: compute covariance
  double cov = 0.0;
  for (int i = 0; i < N; i++) {
    cov += (x[i] - mean_x) * (y[i] - mean_y);
  }
  return cov / (N-1); // Use N-1 for sample covariance
}

double correlationPearson(const float* x, const float* y, size_t N) {
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
double correlation(const float* x, const float* y, int N)
{
  if (N == 0) return 0.0;
  
  double mean_x = 0.0, mean_y = 0.0;
  
  // First pass: compute means
  for (int i = 0; i < N; i++) {
    mean_x += x[i];
    mean_y += y[i];
  }
  mean_x /= N;
  mean_y /= N;
  //printf("Mean:%13.7e %13.7e\n",mean_x,mean_y);
  // Second pass: compute correlation
  double cov = 0.0;
  double stdx = 0;
  double stdy = 0;
  for (int i = 0; i < N; i++) {
    stdx += (x[i] - mean_x)*(x[i] - mean_x);
    stdy += (y[i] - mean_y)*(y[i] - mean_y);
    cov += (x[i] - mean_x) * (y[i] - mean_y);
  }
  if(stdx==0 || stdy==0) return 0;
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
  } else {
    // b is zero; eigenvectors are aligned with x and y axes
    v1[0] = 1.0; v1[1] = 0.0;
    v2[0] = 0.0; v2[1] = 1.0;
  }

  return 1; // Success
}

static void* process_front(void *n_panambio)
{
  Panambio *panam = (Panambio*) n_panambio;
  panam->thr_process();
  return (void*) panam;
}

static void* process_surround(void *n_panambio)
{
  Panambio *panam = (Panambio*) n_panambio;
  panam->thr_process();
  return (void*) panam;
}

Panambio::Panambio(string n_name, int n_mode)
{
  name = n_name;
  quiet = false;
  mode = n_mode;
  prio = 0;
  left_name_in = "";
  left_name_out = "";
  right_name_in = "";
  right_name_out = "";
  mid_left_name_out = "";
  side_left_name_out = "";
  mid_right_name_out = "";
  side_right_name_out = "";
}

Panambio::~Panambio(void)
{
  run = false;
  sem_post(&semaphore); 
  pthread_join(t_proc, NULL);
  free(left_in);
  free(right_in);
  free(left_out);
  free(right_out);
  free(mid_left_out);
  free(mid_right_out);
  free(side_left_out);
  free(side_right_out);
  free(pca.mid_step);
  free(pca.side_step);
  free(pca.mid_left);
  free(pca.side_left);
  free(pca.mid_right);
  free(pca.side_right);
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

bool Panambio::setMainGain(double gain)
{
  gain_main = gain;
  return true;
}

bool Panambio::setAmbGain(double gain)
{
  gain_amb = gain;
  return true;
}

bool Panambio::setSurrGain(double gain)
{
  gain_main_surround = gain;
  return true;
}

void Panambio::setSampleCount(int n_sample_count)
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
  
  mid_left_out = (float*) calloc(sample_count, sizeof(float));
  mid_right_out = (float*) calloc(sample_count, sizeof(float));
  memset(mid_left_out, 0, sample_count);
  memset(mid_right_out, 0, sample_count);
  
  side_left_out = (float*) calloc(sample_count, sizeof(float));
  side_right_out = (float*) calloc(sample_count, sizeof(float));
  memset(side_left_out, 0, sample_count);
  memset(side_right_out, 0, sample_count);
}

void Panambio::setStepsLength(int n_steps_lenght)
{
  overlap = n_steps_lenght;
}


void Panambio::setChannelIn(enum side n_side, string n_channel_in)
{
  if(n_side == LEFT) {
    left_name_in = n_channel_in;
  }
  else {
    right_name_in = n_channel_in;
  } 
}

void Panambio::setChannelOut(enum side n_side, string n_channel_out)
{
  if(n_side == LEFT)
    left_name_out = n_channel_out;
  else if (n_side == RIGHT)
    right_name_out = n_channel_out;
  else if (n_side == MID_LEFT)
    mid_left_name_out = n_channel_out;
  else if (n_side == MID_RIGHT)
    mid_right_name_out = n_channel_out;
  else if (n_side == SIDE_LEFT)
    side_left_name_out = n_channel_out;
  else
    side_right_name_out = n_channel_out;
}

string Panambio::getChannelIn(enum side n_side)
{
  if(n_side == LEFT)
    return left_name_in;
  else 
    return right_name_in;
}

string Panambio::getChannelOut(enum side n_side)
{
  if(n_side == LEFT) 
    return left_name_out;
  else if(n_side == RIGHT) 
    return right_name_out;
  else if(n_side == MID_LEFT) 
    return mid_left_name_out;
  else if(n_side == MID_RIGHT) 
    return mid_right_name_out;
  else if(n_side == SIDE_LEFT) 
    return side_left_name_out;
  else 
    return side_right_name_out;
}

void Panambio::fillInputBuffer(enum side n_side, const float *n_input)
{
  if(n_side == LEFT) {
#ifdef RTDEBUG
    std::cout << "Panambio: copying buffer to left input " << std::endl;
#endif
    memcpy(left_in, n_input, sample_count*sizeof(float));
#ifdef RTDEBUG
    std::cout << "l_in:" << left_in[0] << std::endl;
#endif
  }
  else {
#ifdef RTDEBUG
    std::cout << "Panambio: copying buffer to right input " << std::endl;
#endif
    memcpy(right_in, n_input, sample_count*sizeof(float));
#ifdef RTDEBUG
    std::cout << "r_in:" << left_in[0] << std::endl;
#endif
  }
}

void Panambio::fillOutputBuffer(enum side n_side, float* n_output)
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
  } else if(n_side == MID_LEFT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += mid_left_out[i];
#ifdef RTDEBUG
    std::cout << "l_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == MID_RIGHT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += mid_right_out[i];
#ifdef RTDEBUG
    std::cout << "r_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == SIDE_LEFT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += side_left_out[i];
#ifdef RTDEBUG
    std::cout << "l_out:" << n_output[0] << std::endl;
#endif
  } else if(n_side == SIDE_RIGHT) {
    for(int i = 0; i < sample_count; i++)
      n_output[i] += side_right_out[i];
#ifdef RTDEBUG
    std::cout << "r_out:" << n_output[0] << std::endl;
#endif
  }  
  pthread_mutex_unlock(&mutex);
}

void Panambio::load(int abspri, int policy)
{

  int min, max;

#ifdef RTDEBUG
  std::cout << "Panambio: initial loading " << name << std::endl;
#endif

  pan = 1;
  icorr = 1;

  pca.mid_step = (double*) calloc(overlap*sample_count, sizeof(double));
  pca.side_step = (double*) calloc(overlap*sample_count, sizeof(double));
  pca.mid_left = (double*) calloc(overlap*sample_count, sizeof(double));
  pca.mid_right = (double*) calloc(overlap*sample_count, sizeof(double));
  pca.side_left = (double*) calloc(overlap*sample_count, sizeof(double));
  pca.side_right = (double*) calloc(overlap*sample_count, sizeof(double));

  // Data for covariance calculation
  covM.sum_xy_array = (double*) calloc(2 * overlap - 1, sizeof(double));
  covM.sum_x2_array =  (double*) calloc(2 * overlap - 1, sizeof(double));
  covM.sum_y2_array =  (double*) calloc(2 * overlap - 1, sizeof(double));
  covM.sum_x_array = (double*) calloc(2 * overlap - 1, sizeof(double));
  covM.sum_y_array =  (double*) calloc(2 * overlap - 1, sizeof(double));

  memset(covM.sum_xy_array, 0, (2 * overlap -1)*sizeof(double));
  memset(covM.sum_x2_array, 0, (2 * overlap -1)*sizeof(double));
  memset(covM.sum_y2_array, 0, (2 * overlap -1)*sizeof(double));
  memset(covM.sum_x_array, 0, (2 * overlap -1)*sizeof(double));
  memset(covM.sum_y_array, 0, (2 * overlap -1)*sizeof(double));

  if(mode) {
    // Data for correlation calculation
    icorrv.sum_xy_array = (double*) calloc(2 * ICORRL - 1, sizeof(double));
    icorrv.sum_x2_array =  (double*) calloc(2 * ICORRL - 1, sizeof(double));
    icorrv.sum_y2_array =  (double*) calloc(2 * ICORRL - 1, sizeof(double));
    icorrv.sum_x_array = (double*) calloc(2 * ICORRL - 1, sizeof(double));
    icorrv.sum_y_array =  (double*) calloc(2 * ICORRL - 1, sizeof(double));
    
    memset(icorrv.sum_xy_array, 0, (2 * ICORRL -1)*sizeof(double));
    memset(icorrv.sum_x2_array, 0, (2 * ICORRL -1)*sizeof(double));
    memset(icorrv.sum_y2_array, 0, (2 * ICORRL -1)*sizeof(double));
    memset(icorrv.sum_x_array, 0, (2 * ICORRL -1)*sizeof(double));
    memset(icorrv.sum_y_array, 0, (2 * ICORRL -1)*sizeof(double));
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
    pthread_create(&t_proc, &attr, process_surround, (void *)this);
  else
    pthread_create(&t_proc, &attr, process_front, (void *)this);
  pthread_attr_destroy(&attr);
  pthread_mutex_init(&mutex, NULL);
}

void Panambio::signal(void)
{
#ifdef RTDEBUG
  std::cout << "Panambio: signaling " << name << std::endl;
#endif
  sem_post(&semaphore); 
}

void Panambio::thr_process(void)
{ 
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
  int double_overlap = 2 * overlap;
  double left_main;
  double right_main;
  double left_amb;
  double right_amb;
  double left_surr;
  double right_surr;
  int N = (2 * overlap - 1)*sample_count;

  if(!quiet) {
    std::cout << "Panambio: running thread " << this->name << std::endl;
    }  

  while(run) {
   
    // wait to semaphore signal
    sem_wait(&semaphore);

#ifdef RTDEBUG
    std::cout << "Panambio: processing " << name << std::endl;
#endif

    // process input
    covM.sum_xy_array[2 * overlap - 2] = 0;
    covM.sum_x2_array[2 * overlap - 2] = 0;
    covM.sum_y2_array[2 * overlap - 2] = 0;
    covM.sum_x_array[2 * overlap - 2] = 0;
    covM.sum_y_array[2 * overlap - 2] = 0;

    if(mode) {
      // surround only
      icorrv.sum_xy_array[2 * ICORRL - 2] = 0;
      icorrv.sum_x2_array[2 * ICORRL - 2] = 0;
      icorrv.sum_y2_array[2 * ICORRL - 2] = 0;
      icorrv.sum_x_array[2 * ICORRL - 2] = 0;
      icorrv.sum_y_array[2 * ICORRL - 2] = 0;

      // Correlation
      for (int i = 0; i < sample_count; i++) {
	icorrv.sum_xy_array[2 * ICORRL - 2] += left_in[i] * right_in[i];
	icorrv.sum_x2_array[2 * ICORRL - 2] += left_in[i] * left_in[i];
	icorrv.sum_y2_array[2 * ICORRL - 2] += right_in[i] * right_in[i];
	icorrv.sum_x_array[2 * ICORRL - 2] += left_in[i];
	icorrv.sum_y_array[2 * ICORRL - 2] += right_in[i];
      }
      for(int i = 0; i < 2 * overlap -1; i++) {
	c_sum_xy += icorrv.sum_xy_array[i];
	c_sum_x2 += icorrv.sum_x2_array[i];
	c_sum_y2 += icorrv.sum_y2_array[i];
	c_sum_x += icorrv.sum_x_array[i];
	c_sum_y += icorrv.sum_y_array[i];
      }
      if(ICORRL*sample_count*c_sum_x2 <= c_sum_x*c_sum_x || ICORRL*sample_count*c_sum_y2 <= c_sum_y*c_sum_y)
	icorr = 1;
      else 
	icorr = (ICORRL*sample_count*c_sum_xy - c_sum_x*c_sum_y) / sqrt((ICORRL*sample_count*c_sum_x2 - c_sum_x*c_sum_x)* (ICORRL*sample_count*c_sum_y2 - c_sum_y*c_sum_y));
      pan = 0.8 + icorr * 0.2;
      //pan = 1.0 - exp(PANCOEFF*icorr)*0.5;
      //pan = (1.0 -exp(PANCOEFF*icorr))/(1-exp(PANCOEFF));
    }
    
    for (int i = 0, j = (overlap - 1) * sample_count; i < sample_count; i++, j++) {
      pca.mid_step[j] = left_in[i] + right_in[i];
      pca.side_step[j] = pan*(left_in[i] - right_in[i]);
      // Covariances
      covM.sum_xy_array[2 * overlap - 2] += pca.mid_step[j] * pca.side_step[j];
      covM.sum_x2_array[2 * overlap - 2] += pca.mid_step[j] * pca.mid_step[j];
      covM.sum_y2_array[2 * overlap - 2] += pca.side_step[j] * pca.side_step[j];
      covM.sum_x_array[2 * overlap - 2] += pca.mid_step[j];
      covM.sum_y_array[2 * overlap - 2] += pca.side_step[j];
    }
    for(int i = 0; i < 2 * overlap -1; i++) {
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
    
    // Output: surround
    if(mode) {
      // Surround / Rear calculation 
      for(int i = 0; i < overlap * sample_count; i ++) {
	side_factor = eigvectors[1][0] * pca.mid_step[i] + eigvectors[1][1] * pca.side_step[i];
	pca.side_left[i] += side_factor * eigvectors[1][0]; // = side_left
	pca.side_right[i] += side_factor * eigvectors[1][1]; // = side_right
      }
      for(int  i = 0; i < sample_count; i++) {
	left_surr = (pca.side_left[i] + pca.side_right[i])/(double_overlap);
	right_surr = (pca.side_left[i] - pca.side_right[i])/(double_overlap);
	left_out[i]  = gain_main_surround*(left_surr); // right_surround
	right_out[i] = gain_main_surround*(right_surr); // left_surround
	side_left_out[i] = left_out[i];
	side_right_out[i] = right_out[i];
      }
    } else {
      // Main / Front calculation
      for(int i = 0; i < overlap * sample_count; i ++) {
	mid_factor =  eigvectors[0][0] * pca.mid_step[i] + eigvectors[0][1] * pca.side_step[i];
	side_factor = eigvectors[1][0] * pca.mid_step[i] + eigvectors[1][1] * pca.side_step[i];
	pca.mid_left[i] += mid_factor * eigvectors[0][0]; // = mid_left
	pca.mid_right[i] += mid_factor * eigvectors[0][1]; // = mid_right
	pca.side_left[i] += side_factor * eigvectors[1][0]; // = side_left
	pca.side_right[i] += side_factor * eigvectors[1][1]; // = side_right
      }
      for(int  i = 0; i < sample_count; i++) {
	left_main = (pca.mid_left[i] + pca.mid_right[i])/(double_overlap);
	right_main = (pca.mid_left[i] - pca.mid_right[i])/(double_overlap);
	left_amb = (pca.side_left[i] + pca.side_right[i])/(double_overlap);
	right_amb = (pca.side_left[i] - pca.side_right[i])/(double_overlap);
	left_out[i]  = gain_main*left_main + gain_amb*left_amb;
	right_out[i] = gain_main*right_main + gain_amb*right_amb;
	mid_left_out[i] = gain_main*left_main;
        mid_right_out[i] = gain_main*right_main;
	side_left_out[i] = gain_amb*left_amb;
	side_right_out[i] = gain_amb*right_amb;
      }
    }
    
    for(int i = 0, j = sample_count; i < sample_count *(overlap - 1); i++, j++) {
      pca.mid_left[i] = pca.mid_left[j];
      pca.mid_right[i] = pca.mid_right[j];
      pca.side_left[i] = pca.side_left[j];
      pca.side_right[i] = pca.side_right[j];
      pca.mid_step[i] = pca.mid_step[j];
      pca.side_step[i] = pca.side_step[j];
    }
    
    for(int i = sample_count *(overlap - 1); i < sample_count * overlap; i++) {
      pca.mid_left[i] = 0;
      pca.mid_right[i] = 0;
      pca.side_left[i] = 0;
      pca.side_right[i] = 0;
    }
    
    for(int i = 0; i < 2 * overlap - 2; i++) {
      covM.sum_xy_array[i] = covM.sum_xy_array[i + 1];
      covM.sum_x2_array[i] = covM.sum_x2_array[i + 1];
      covM.sum_y2_array[i] = covM.sum_y2_array[i + 1];
      covM.sum_x_array[i] = covM.sum_x_array[i + 1];
      covM.sum_y_array[i] = covM.sum_y_array[i + 1];
    }
    if(mode) {
      // surround only
      for(int i = 0; i < 2 * ICORRL - 2; i++) {
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
    std::cout << "Panambio: stopping thread " << this->name << std::endl;
  } 
}




