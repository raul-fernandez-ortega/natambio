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
  pan_scale_target = 0.0f;
  pan_scale_now = 0;
  pan_a = 1.0;
  pan_b = 0.0;
  sample_count = 0;
  /* Replaced by setSampleRate() once JACK's rate is known; until then the
     value 48 kHz would give, so a gain set before that still fades. */
  sample_rate = 48000;
  ramp_inc = 1.0f / 1536.0f;
  gain_c1 = gain_c2 = gain_c2_rear = 0.0;
  gain_c1_target = gain_c2_target = gain_c2_rear_target = 0.0f;
  gain_c1_db = gain_c2_db = gain_c2_rear_db = NA_NAE_GAIN_MIN_DB;
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

/* The dB face of a linear gain out of the configuration. Zero is what naconf
   leaves the gains a mode does not use at -- beta parses no <front_gain> --
   and has no logarithm, so it reports as the floor, which is what it is. */
static double gain_to_db(double gain)
{
  if(gain <= 0.0)
    return NA_NAE_GAIN_MIN_DB;
  double db = TO_DB(gain);
  if(db > NA_NAE_GAIN_MAX_DB) db = NA_NAE_GAIN_MAX_DB;
  if(db < NA_NAE_GAIN_MIN_DB) db = NA_NAE_GAIN_MIN_DB;
  return db;
}

/* Configuration time: the current value as well as the target, so the first
   block comes out at the configured gain instead of fading up to it. The
   worker thread is not running yet -- load() starts it -- so writing both is
   safe here and nowhere else. */
bool NAE::setC1Gain(double gain)
{
  gain_c1_db = gain_to_db(gain);
  gain_c1_target = (float)gain;
  gain_c1 = gain;
  return true;
}

bool NAE::setC2Gain(double gain)
{
  gain_c2_db = gain_to_db(gain);
  gain_c2_target = (float)gain;
  gain_c2 = gain;
  return true;
}

bool NAE::setC2RearGain(double gain)
{
  gain_c2_rear_db = gain_to_db(gain);
  gain_c2_rear_target = (float)gain;
  gain_c2_rear = gain;
  return true;
}

double *NAE::gainDbSlot(enum nae_gain which)
{
  if(which == NAE_GAIN_FRONT)
    return &gain_c1_db;
  if(which == NAE_GAIN_AMB)
    return &gain_c2_db;
  return &gain_c2_rear_db;
}

volatile float *NAE::gainTargetSlot(enum nae_gain which)
{
  if(which == NAE_GAIN_FRONT)
    return &gain_c1_target;
  if(which == NAE_GAIN_AMB)
    return &gain_c2_target;
  return &gain_c2_rear_target;
}

/* Which gains a mode has any use for. Alpha splits the pair into a principal
   and an ambience component and mixes both back, so it reads gain_c1 and
   gain_c2; beta computes the ambience alone and sends it to the rears, so it
   reads gain_c2_rear and nothing else. The other gains are still kept, set and
   reported -- they multiply nothing while the engine is in this mode, and a
   mode does not change while natambio runs, so this is a statement about the
   configuration rather than about the moment. */
bool NAE::gainActive(enum nae_gain which)
{
  if(mode)
    return which == NAE_GAIN_REAR;
  return which == NAE_GAIN_FRONT || which == NAE_GAIN_AMB;
}

double NAE::gainDb(enum nae_gain which)
{
  return *gainDbSlot(which);
}

/* Called from the remote manager's thread. The dB value belongs to that thread
   and the linear target is the one word the worker reads, written last and in
   one store -- the discipline ioJack::adjustPortGain() keeps for a port gain,
   and for the same reason: the worker is in the middle of a block and must
   never see half a change. Unlike the port commands this one is absolute, so
   the clamp is not what stops a repeated command from running away; it is
   what stops a single mistyped number. */
double NAE::setGainDb(enum nae_gain which, double db)
{
  if(db > NA_NAE_GAIN_MAX_DB) db = NA_NAE_GAIN_MAX_DB;
  if(db < NA_NAE_GAIN_MIN_DB) db = NA_NAE_GAIN_MIN_DB;
  *gainDbSlot(which) = db;
  *gainTargetSlot(which) = (float)FROM_DB(db);
  return db;
}

/* The width matrix for a given <pan_scale>. Its own function because the worker
   thread now derives it too, once a block, from wherever the slew has got to:
   two cosines a block is nothing, and deriving both weights from one scalar is
   what keeps them a matrix that means something at every point of a move. */
static void pan_weights(double scale, double *a, double *b)
{
  double phi = (M_PI/4.0)*(1.0 - scale);
  double wm = M_SQRT2*cos(phi);
  double ws = M_SQRT2*sin(phi);
  *a = 0.5*(wm + ws);
  *b = 0.5*(wm - ws);
}

/* Called from the remote manager's thread: one store into the word the worker
   reads, which is the scalar rather than the weights. Refused, and nothing
   touched, outside the parameter's domain -- see NA_NAE_PAN_MIN/MAX. */
bool NAE::setLivePanScale(double n_scale)
{
  if(n_scale < NA_NAE_PAN_MIN || n_scale > NA_NAE_PAN_MAX)
    return false;
  pan_scale = n_scale;
  pan_scale_target = (float)n_scale;
  return true;
}

void NAE::setPanScale(double n_scale)
{
  pan_scale = n_scale;
  pan_scale_target = (float)n_scale;
  pan_scale_now = n_scale;
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
  pan_weights(pan_scale, &pan_a, &pan_b);
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

void NAE::setSampleRate(int n_sample_rate)
{
  sample_rate = n_sample_rate;
  /* 32 ms, the figure ioJack::global_init() ramps at: a gain arriving here and
     one arriving at a port move together, and neither is heard as a step. */
  if(sample_rate > 0)
    ramp_inc = 1.0f / (0.032f * (float)sample_rate);
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

/* One period, in four steps. The split is what lets a second engine
   (nae_erb.cpp) replace the decomposition and inherit everything else; the
   contract between them is written out in nae.hpp. Nothing here was changed on
   the way out of thr_process() except where the width weights are kept, which
   moved from locals to members so that all four steps can see them.

   One real change: the mutex now covers the write to the output buffers and
   nothing else. It used to span the accumulation and the buffer shifts as well,
   which fillOutputBuffer() has no interest in -- it reads left_out and its four
   companions, and those are written in emitBlock() alone. */

void NAE::prepareBlock(void)
{
  double c_sum_xy = 0;
  double c_sum_x2 = 0;
  double c_sum_y2 = 0;
  double c_sum_x = 0;
  double c_sum_y = 0;

    /* The width across this block: the scalar slewed towards its target, and
       the matrix at each end of that move. Both loops below walk the same
       sample_count with the same step, so the correlation in beta mode sees
       the pair exactly as the decomposition does. The two weights are
       interpolated between two matrices rather than recomputed per sample --
       a cosine a sample would buy an exactness that a 30 ms move between two
       neighbouring widths has no room to be wrong by. */
  double ps0 = pan_scale_now;
  double ps1 = slewGain(ps0, (double)pan_scale_target);
  double pa1, pb1;
  pan_a_begin = pan_a;
  pan_b_begin = pan_b;
  pan_weights(ps1, &pa1, &pb1);
  pan_a_step = (pa1 - pan_a_begin)/(double)sample_count;
  pan_b_step = (pb1 - pan_b_begin)/(double)sample_count;
  /* Where the block will leave the width. Committed here rather than at the end
     because nothing between reads it: the loops walk from pan_a_begin by
     pan_a_step and never look at pan_a again. */
  pan_scale_now = ps1;
  pan_a = pa1;
  pan_b = pb1;

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
      double pa = pan_a_begin, pb = pan_b_begin;
      for (int i = 0; i < sample_count; i++, pa += pan_a_step, pb += pan_b_step) {
        double l = pa*left_in[i] + pb*right_in[i];
        double r = pb*left_in[i] + pa*right_in[i];
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
}

void NAE::decompose(void)
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
  int N = (covsteps)*sample_count;

    // process input
    covM.sum_xy_array[covsteps - 1] = 0;
    covM.sum_x2_array[covsteps - 1] = 0;
    covM.sum_y2_array[covsteps - 1] = 0;
    covM.sum_x_array[covsteps - 1] = 0;
    covM.sum_y_array[covsteps - 1] = 0;

    double pa = pan_a_begin, pb = pan_b_begin;
    for (int i = 0, j = (covsteps - 1) * sample_count; i < sample_count;
         i++, j++, pa += pan_a_step, pb += pan_b_step) {
      // Width first, then the decomposition works on the pair as it leaves it.
      double l = pa*left_in[i] + pb*right_in[i];
      double r = pb*left_in[i] + pa*right_in[i];
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
    

  if(mode) {
      // Beta ambient calculation
      for(int i = 0; i < covsteps * sample_count; i ++) {
        c2_factor = eigvectors[1][0] * pca.mid_step[i] + eigvectors[1][1] * pca.side_step[i];
        pca.c2_mid[i] += c2_factor * eigvectors[1][0];
        pca.c2_side[i] += c2_factor * eigvectors[1][1];
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
  }
}

void NAE::emitBlock(void)
{
  int norm_covsteps = covsteps + 1;
  double c1_left;
  double c1_right;
  double c2_left;
  double c2_right;

  pthread_mutex_lock(&mutex);
  if(mode) {
      /* The rear gain across this block: where the last one left it, to where
         the slew takes it, interpolated sample by sample. A gain arriving from
         the remote manager is heard as a short fade this way; applied whole at
         the block boundary it would be a step, which is a click, and through
         the rears a thump. Costs one add per sample and nothing at all once
         the gain has arrived, when both ends are the same number. */
      double gr0 = gain_c2_rear;
      double gr1 = slewGain(gr0, (double)gain_c2_rear_target);
      double gr_step = (gr1 - gr0)/(double)sample_count;
      double gr = gr0;
      for(int  i = 0; i < sample_count; i++, gr += gr_step) {
        c2_left = (pca.c2_mid[i] + pca.c2_side[i])/(norm_covsteps);
        c2_right = (pca.c2_mid[i] - pca.c2_side[i])/(norm_covsteps);
        left_out[i]  = gr*c2_left;
        right_out[i] = gr*c2_right;
        c2_left_out[i] = left_out[i];
        c2_right_out[i] = right_out[i];
      }
      gain_c2_rear = gr1;
  } else {
      /* Both gains slewed across the block, each towards its own target and
         each on its own line: the two components are mixed back together here,
         and a step in either is a step in the sum. See the beta branch. */
      double g1_0 = gain_c1;
      double g1_1 = slewGain(g1_0, (double)gain_c1_target);
      double g1_step = (g1_1 - g1_0)/(double)sample_count;
      double g2_0 = gain_c2;
      double g2_1 = slewGain(g2_0, (double)gain_c2_target);
      double g2_step = (g2_1 - g2_0)/(double)sample_count;
      double g1 = g1_0;
      double g2 = g2_0;
      for(int  i = 0; i < sample_count; i++, g1 += g1_step, g2 += g2_step) {
        c1_left = (pca.c1_mid[i] + pca.c1_side[i])/(norm_covsteps);
        c1_right = (pca.c1_mid[i] - pca.c1_side[i])/(norm_covsteps);
        c2_left = (pca.c2_mid[i] + pca.c2_side[i])/(norm_covsteps);
        c2_right = (pca.c2_mid[i] - pca.c2_side[i])/(norm_covsteps);
        left_out[i]  = g1*c1_left + g2*c2_left;
        right_out[i] = g1*c1_right + g2*c2_right;
        c1_left_out[i] = g1*c1_left;
        c1_right_out[i] = g1*c1_right;
        c2_left_out[i] = g2*c2_left;
        c2_right_out[i] = g2*c2_right;
      }
      gain_c1 = g1_1;
      gain_c2 = g2_1;
  }
  pthread_mutex_unlock(&mutex);
}

void NAE::advanceBlock(void)
{
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
    }
}

void NAE::thr_process(void)
{
  if(!quiet) {
    std::cout << "NAE: running thread " << this->name << std::endl;
  }

  while(run) {

    // wait to semaphore signal
    sem_wait(&semaphore);

#ifdef RTDEBUG
    std::cout << "NAE: processing " << name << std::endl;
#endif

    /* From here to the end of the block is what this engine costs per period,
       and the clock starts after the wait rather than before it: the time
       spent blocked on the semaphore is the callback's period, not this
       thread's work, and counting it would report a load of 100% on an engine
       that is idle. */
    proc_time.begin();

    prepareBlock();
    decompose();
    emitBlock();
    advanceBlock();

    proc_time.end();
  }
  if(!quiet) {
    std::cout << "NAE: stopping thread " << this->name << std::endl;
  }
}
