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

// Default time constant of the NAE placement smoothing, seconds.
#define NAE_PAN_TAU_DEF 0.5

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
  C1_LEFT,
  C1_RIGHT,
  C2_LEFT,
  C2_RIGHT
};

struct s_nae {
  string name;
  int mode;
  double gain_c1;
  double gain_c2;
  double gain_c2_rear;
  double pan_scale;      // width of the input pair, [-1, 1]
  double pan_rotate;     // rotation of the placement frame, [-1, 1]
  double pan_rotate_tau; // time constant of the placement smoothing, seconds
  int steps_length;
  string left_in;
  string right_in;
  string left_out;
  string right_out;
  string c1_left_out;
  string c1_right_out;
  string c2_left_out;
  string c2_right_out;
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

// One speaker's parameters in an asymmetric <xtc_asym> block: exactly the four
// values that define one acoustic path (one G of the model). Mirrors
// xtc_asym_side in lib/xtc_asym.h, which is plain C and cannot be included here.
struct xtc_side {
  int itd_us;           // inter-aural time difference, microseconds
  double ild_db;        // inter-aural level difference per step, dB
  double ild_alpha;     // log-empirical ILD model scale factor
  int azimuth_deg;      // source azimuth, degrees
};

// Both <xtc> and <xtc_asym> land here; `asymmetric` says which set of fields is
// live. The symmetric block yields two coeffs (direct + cross), the asymmetric
// one three (direct + one cross per speaker), because in an asymmetric layout
// the two cross filters differ while the direct filter is shared -- it depends
// only on the product G_l*G_r. See docs/xtc/xtc_no_simetrico_es.md.
struct xtc {
  bool asymmetric;          // false: <xtc>;  true: <xtc_asym>
  string direct_name;       // name of the resulting direct-path coeff (both)
  string cross_name;        // <xtc>: name of the resulting cross-path coeff
  string cross_left_name;   // <xtc_asym>: cross coeff feeding the left speaker
  string cross_right_name;  // <xtc_asym>: cross coeff feeding the right speaker
  int itd_us;               // <xtc>: inter-aural time difference, microseconds
  double ild_db;            // <xtc>: inter-aural level difference per step, dB
  double ild_alpha;         // <xtc>: log-empirical ILD model scale factor
  int azimuth_deg;          // <xtc>: source azimuth, degrees
  struct xtc_side left;     // <xtc_asym>: left speaker parameters
  struct xtc_side right;    // <xtc_asym>: right speaker parameters
  int filter_len;           // filter length, samples (sample rate is JACK's)
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
