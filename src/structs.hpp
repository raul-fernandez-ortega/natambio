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
  C2_RIGHT,
  /* The part of C2 that sits beyond the inter-channel level difference the
     ambience is allowed to carry: one channel of it is always exactly zero,
     because only the louder one is ever cut. See NAE::emitBlock(). */
  LAT_LEFT,
  LAT_RIGHT
};

/* Which of an NAE's three gains is being named. Only some of them do anything
   in a given mode -- see NAE::gainActive() -- but all three are kept, so a
   value set on the wrong one is remembered rather than lost. Here rather than
   in nae.hpp because the configuration names the same three (<front_gain>,
   <ambience_gain>, <rear_gain>) and has no business knowing about the engine. */
enum nae_gain {
  NAE_GAIN_FRONT,   /* gain_c1 / <front_gain>: principal component, alpha only */
  NAE_GAIN_AMB,     /* gain_c2 / <ambience_gain>: ambience, alpha only */
  NAE_GAIN_REAR,    /* gain_c2_rear / <rear_gain>: to the rears, beta only */
  /* gain_lat / <lateral_gain>: the lateral part of the ambience, and the one
     gain of the four that means something in BOTH modes. It is the companion
     of whichever gain carries the ambience there -- <ambience_gain> in alpha,
     <rear_gain> in beta -- because the two halves it splits are the two halves
     of one signal: set equal to that one, the engine does exactly what it did
     before this existed. */
  NAE_GAIN_LAT
};

struct s_nae {
  string name;
  int mode;
  double gain_c1;
  double gain_c2;
  double gain_c2_rear;
  double gain_lat;
  /* <lateral_threshold_db> and <lateral_knee_db>: where the ambience stops
     being ambience, and how rounded the corner there is. Defaults in nae.hpp;
     the domain, knee below twice the threshold, is checked at parse time. */
  double lat_threshold_db;
  double lat_knee_db;
  /* Whether the file gave <lateral_gain>. Absent, it is not a default number
     but the gain the mode already carries the ambience with, which is what
     makes a configuration written before the lateral pair existed sound
     exactly as it did: the two halves go back out at one gain and sum to the
     C2 they were cut from. */
  bool gain_lat_set;
  double pan_scale;      // width of the input pair, [-1, 1]
  int steps_length;
  /* What <steps_length_ms> asked for, when it is what set it; negative when
     <steps_length> did or when neither was given. For the report only. */
  double steps_length_ms_used;
  /* Neither tag was given and the window came from the period. For the report
     only: a reader has to be able to tell a chosen 3 from a derived one. */
  bool steps_length_default;
  string left_in;
  string right_in;
  string left_out;
  string right_out;
  string c1_left_out;
  string c1_right_out;
  string c2_left_out;
  string c2_right_out;
  string lat_left_out;
  string lat_right_out;
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
  /* Samples of pure shift that the response was DESIGNED around, as opposed to
     the group delay every filter has. The difference matters because this one
     is latency and can be declared to JACK as such, and a group delay is a
     property of the response that varies with frequency and cannot.

     Only the XTC pair carries one: its fractional-delay design has a two-sided
     impulse response and xtc_model_delay() moves BOTH filters forward so that
     nothing is clipped at n = 0. XTC depends only on the delay between the two,
     so a shift common to both costs latency and nothing else. The crossover and
     the loudness filter are generated in minimum phase and carry none.

     A coeff built by chaining others carries the sum of theirs: convolving two
     responses adds their delays. */
  int bulk_delay;
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

/* Which phase a generated FIR is delivered in. The magnitude is the same
   either way -- that is the whole point of the pair -- so this chooses between
   two costs and not between two filters:

   MINIMUM has no pre-ringing and no latency to compensate, and is what a
   loudspeaker crossover almost always wants, the ear being least forgiving of
   a pre-echo in the bass. It rotates phase across the corner.

   LINEAR keeps every frequency on the same time base, which is what a
   decomposition wants when its two halves are summed back together, and pays
   for it with (length-1)/2 samples of pure delay and a symmetric impulse
   response -- so the ring is half in front of the transient. That delay is
   declared as bulk_delay, so JACK is told the truth about the path; aligning
   it against a path that does not carry the filter is still the
   configuration's job, and <convol>/<delay> is where it is done. */
enum fir_phase {
  FIR_PHASE_LINEAR,
  FIR_PHASE_MINIMUM
};

/* What shape a <fir_filter> block asks for. A band-pass is a high-pass skirt
   and a low-pass skirt with their own slopes, not one parameter. */
enum fir_type {
  FIR_TYPE_LOWPASS,
  FIR_TYPE_HIGHPASS,
  FIR_TYPE_BANDPASS
};

struct fir_filter {
  string name;          // name of the resulting coeff
  enum fir_type type;
  enum fir_phase phase;
  /* The band edges. A low-pass or a high-pass uses low_freq alone, whichever
     end it is; a band-pass uses both, low_freq being the high-pass skirt and
     high_freq the low-pass one. Parsed from <frequency> in the first case and
     from <low_frequency>/<high_frequency> in the second. */
  double low_freq;
  double high_freq;
  double low_slope;     // dB/octave of the skirt below low_freq
  double high_slope;    // dB/octave of the skirt above high_freq
  double gain;          // pass-band gain, dB
  int filter_len;       // samples; the sample rate is JACK's
};

struct lowhigh {
  string low_name;      // name of the resulting low-pass coeff
  string high_name;     // name of the resulting high-pass coeff
  double frequency;     // crossover (cut-off) frequency, Hz
  double db_octave;     // low-pass roll-off slope, dB per octave
  double gain;          // pass-band gain, dB (+ amplifies, - attenuates)
  int filter_len;       // low/high filter length, samples (sample rate is JACK's)
  /* <phase>. Minimum unless the file says otherwise, which is what this block
     has always delivered. In linear phase the complement is still exact -- the
     delta it is subtracted from sits at the low-pass's own group delay -- and
     both coeffs carry that delay as their bulk_delay. */
  enum fir_phase phase;
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
  /* What <delay_ms> asked for, when it is what set the delay; negative when
     <delay> did or when neither was given. Kept only so the report can say what
     a duration became in samples, which is the one thing a reader cannot work
     out from the file alone. */
  double delay_ms_used;
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
