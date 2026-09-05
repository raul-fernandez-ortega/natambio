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

/* The way back. Undefined at zero and below, as the logarithm is: every caller
   here has a floor of its own to report instead (the gain clamps), and one
   picked in this macro would be the wrong one for somebody. */
#ifndef TO_DB
#define TO_DB(g) (20.0 * log10(g))
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
  C1_LEFT,
  C1_RIGHT,
  C2_LEFT,
  C2_RIGHT
};

/* Which of an NAE's three gains is being named. Only some of them do anything
   in a given mode -- see NAE::gainActive() -- but all three are kept, so a
   value set on the wrong one is remembered rather than lost. Here rather than
   in nae.hpp because the configuration names the same three (<front_gain>,
   <ambience_gain>, <rear_gain>) and has no business knowing about the engine. */
enum nae_gain {
  NAE_GAIN_FRONT,   /* gain_c1 / <front_gain>: principal component, alpha only */
  NAE_GAIN_AMB,     /* gain_c2 / <ambience_gain>: ambience, alpha only */
  NAE_GAIN_REAR     /* gain_c2_rear / <rear_gain>: to the rears, beta only */
};

struct s_nae {
  string name;
  int mode;
  /* Which engine: false for <nae>, true for <nae_erb>. The two tags parse
     through the same function and share every field above the three below,
     because <nae_erb> IS an NAE in everything but the decomposition. */
  bool erb;
  double erb_cov_window_ms;  // <cov_window_ms>: the analysis window
  double erb_delta_erb;      // <delta_erb>: centre spacing, in ERB units
  double erb_band_min_hz;    // <band_min_hz>: the width floor, in Hz
  double gain_c1;
  double gain_c2;
  double gain_c2_rear;
  double pan_scale;      // width of the input pair, [-1, 1]
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
  // The recursion always runs at the exact, unrounded ITD, with the bulk delay
  // that path needs fixed at XTC_DEFAULT_MODEL_DELAY. Neither is configurable
  // any more, so neither is carried here: build_xtc_coeffs() passes both to
  // process() directly.
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
  double gain;          // port gain, dB (+ amplifies, - attenuates)
};

struct jackclient {
  string name;
  vector<struct jackport*> inports;
  vector<struct jackport*> outports;
};

#endif
