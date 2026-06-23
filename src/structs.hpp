/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */
#ifndef _NA_STRUCTS_HPP_
#define _NA_STRUCTS_HPP_

#ifdef __cplusplus
extern "C" {
#endif

#include <sndfile.h>

#ifdef __cplusplus
}
#endif

#include <vector>
#include <string>

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

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

using namespace std;

enum side {
  LEFT,
  RIGHT,
  MID_LEFT,
  MID_RIGHT,
  SIDE_LEFT,
  SIDE_RIGHT
};

struct s_nae {
  string name;
  int mode;
  double gain_main;
  double gain_amb;
  double gain_surr;
  int steps_length;
  string left_in;
  string right_in;
  string left_out;
  string right_out;
  string mid_left_out;
  string mid_right_out;
  string side_left_out;
  string side_right_out;
};

struct coeff {
  string name;
  string filename;
  int channel;
  int skip;
  sf_count_t length;
  float scale;
  float *coeffs;
  SF_INFO snfinfo;
  vector<string> convol_coeffs;  // names of <convol_coeff> coeffs to convolve to build this one
};

struct xtc {
  string direct_name;   // name of the resulting direct-path coeff
  string cross_name;    // name of the resulting cross-path coeff
  int itd_us;           // inter-aural time difference, microseconds
  double ild_db;        // inter-aural level difference per step, dB
  double ild_alpha;     // log-empirical ILD model scale factor
  int azimuth_deg;      // source azimuth, degrees
  int filter_len;       // direct/cross filter length, samples (sample rate is JACK's)
};

struct lowhigh {
  string low_name;      // name of the resulting low-pass coeff
  string high_name;     // name of the resulting high-pass coeff
  double frequency;     // crossover (cut-off) frequency, Hz
  double db_octave;     // low-pass roll-off slope, dB per octave
  double gain;          // pass-band gain, dB (+ amplifies, - attenuates)
  int filter_len;       // low/high filter length, samples (sample rate is JACK's)
};

struct loudness {
  string name;          // name of the resulting (minimum-phase) coeff
  string model;         // equal-loudness model id (see loudness.h)
  double phon;          // target loudness level, phon
  double ref_phon;      // reference loudness level subtracted from the target
  int filter_len;       // filter length, samples (sample rate is JACK's)
};

struct convol {
  int index;
  string name;
  string coeff_name;
  int delay;
  float scale;
  vector<string> from_inputs;
  vector<string> to_outputs;
  vector<string> from_convols;
  vector<string> from_nae;
};

struct jackport {
  string name;
  string destname;
};

struct jackclient {
  string name;
  vector<struct jackport*> inports;
  vector<struct jackport*> outports;
};

#endif
