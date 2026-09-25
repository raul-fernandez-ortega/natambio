/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

/* nae_bench -- run one NAE engine over a WAV, off line, driving it exactly as
 * the JACK callback does.
 *
 * The engine is a thread behind a semaphore, so the only honest way to test it
 * is to feed it the way ioJack::na_process_callback() feeds it, in the same
 * order:
 *
 *     fillInputBuffer(LEFT)  / fillInputBuffer(RIGHT)   -- this period's input
 *     fillOutputBuffer(...)                             -- the PREVIOUS period's
 *     signal()                                          -- go
 *
 * The outputs are read before the signal, which is what the callback does and
 * why the result is one period behind the input. Everything else -- the gains
 * at unity, the width, the covariance length -- is set the way a configuration
 * would set it.
 *
 * There is no end-of-block handshake to wait on: the callback does not wait for
 * the engine either, it just signals it once a period and reads whatever is
 * there next time. Off line that would let the worker slip a VARIABLE number of
 * blocks behind, and two runs of the same file would not line up. Hence the
 * settle time after each signal, and hence --check, which runs the file twice
 * and reports whether the two passes are bit for bit identical. Nothing
 * measured here means anything until that check passes.
 *
 * Its reason for existing is regression: the C1/C2 of a build before a change
 * against the C1/C2 of the build after it, sample for sample.
 *
 *     make
 *     ./nae_bench in.wav c1.wav c2.wav [mode] [frame] [covsteps] [pan] [us]
 */

extern "C" {
#include <sndfile.h>
#include <unistd.h>
#include <sched.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "nae.hpp"

static void usage(const char *me)
{
  fprintf(stderr,
          "usage: %s <in.wav> <out_c1.wav> <out_c2.wav>\n"
          "          [mode 0|1] [frame_size] [covsteps] [pan_scale] [settle_us]\n"
          "       %s --check <in.wav> [mode] [frame_size] [covsteps] [pan] [us]\n"
          "\n"
          "  --lat-split <T> <W>   the lateral cut: threshold and knee, in dB.\n"
          "                        Default 5 and 5, as <lateral_threshold_db>\n"
          "                        and <lateral_knee_db> default.\n"
          "  --lat <file.wav>      also write the LATERAL pair -- the part of\n"
          "                        C2 beyond the level difference the ambience\n"
          "                        is allowed to carry. Without it that pair is\n"
          "                        still computed, and still checked, just not\n"
          "                        written. out_c2.wav is the AMBIENCE half\n"
          "                        alone; the two sum back to the whole of C2.\n"
          "\n"
          "  mode       0 = alpha (front), 1 = beta (rear).   Default 0\n"
          "  frame_size JACK period in samples.               Default 256\n"
          "  covsteps   <steps_length>.                       Default 3\n"
          "  pan_scale  <pan_scale>, [-1, 1].                 Default 0\n"
          "  settle_us  wait after each signal().             Default 8000\n"
          "\n"
          "  --check runs the file twice and reports whether the two passes\n"
          "  agree bit for bit. Do that before trusting any output.\n",
          me, me);
}

/* One pass over the file. The components come back interleaved, C1 and C2, in
   double: the engine works in double and the point of this is to compare two
   builds exactly, so nothing is rounded on the way out. */
static bool run_pass(const char *in_path, int mode, int frame, int covsteps,
                     double pan_scale, int settle_us,
                     double lat_t, double lat_k,
                     std::vector<double>& c1, std::vector<double>& c2,
                     std::vector<double>& lat, int *samplerate)
{
  SF_INFO info;
  memset(&info, 0, sizeof(info));
  SNDFILE *in = sf_open(in_path, SFM_READ, &info);
  if(in == NULL) {
    fprintf(stderr, "nae_bench: cannot open %s: %s\n", in_path, sf_strerror(NULL));
    return false;
  }
  if(info.channels != 2) {
    fprintf(stderr, "nae_bench: %s is not stereo (%d channels)\n", in_path, info.channels);
    sf_close(in);
    return false;
  }
  *samplerate = info.samplerate;

  NAE nae("bench", mode);
  nae.setQuiet();
  /* Linear, as the configuration parses them: unity everywhere, so what comes
     out is the decomposition and not a mix of it. */
  nae.setC1Gain(1.0);
  nae.setC2Gain(1.0);
  nae.setC2RearGain(1.0);
  /* Unity like the rest, and not left where the constructor puts it. The
     lateral half of the ambience goes out at its own gain, and at the
     constructor's zero the C2 of this bench would be missing whatever the cut
     took out of it -- silently, since the ambience half is a perfectly
     plausible signal on its own. A configuration never reaches that state
     (naconf gives <lateral_gain> the mode's ambience gain when the file does
     not name it); a caller that builds an NAE by hand can. */
  nae.setLatGain(1.0);
  if(!nae.setLateralSplit(lat_t, lat_k)) {
    fprintf(stderr, "nae_bench: lateral split %.2f/%.2f refused "
            "(threshold > 0, knee >= 0 and below twice the threshold)\n",
            lat_t, lat_k);
    sf_close(in);
    return false;
  }
  nae.setPanScale(pan_scale);
  nae.setSampleCount(frame);
  nae.setSampleRate(info.samplerate);
  nae.setCovStepsLength(covsteps);
  /* SCHED_OTHER: this is not a real-time test and asking for SCHED_FIFO would
     need privileges the bench has no business wanting. */
  nae.load(0, SCHED_OTHER);

  std::vector<float> inter(frame * 2, 0.0f);
  std::vector<float> in_l(frame, 0.0f), in_r(frame, 0.0f);
  std::vector<float> o_c1l(frame), o_c1r(frame), o_c2l(frame), o_c2r(frame);
  std::vector<float> o_latl(frame), o_latr(frame);

  c1.clear();
  c2.clear();
  lat.clear();

  sf_count_t got;
  bool more = true;
  while(more) {
    memset(&inter[0], 0, inter.size() * sizeof(float));
    got = sf_readf_float(in, &inter[0], frame);
    if(got < frame)
      more = false;                 /* last, short block: zero-padded */
    for(int i = 0; i < frame; i++) {
      in_l[i] = inter[2 * i];
      in_r[i] = inter[2 * i + 1];
    }

    nae.fillInputBuffer(LEFT, &in_l[0]);
    nae.fillInputBuffer(RIGHT, &in_r[0]);

    /* fillOutputBuffer SUMS into the caller's buffer, as it does for a JACK
       port several engines may write to, so the buffer is cleared first. */
    memset(&o_c1l[0], 0, frame * sizeof(float));
    memset(&o_c1r[0], 0, frame * sizeof(float));
    memset(&o_c2l[0], 0, frame * sizeof(float));
    memset(&o_c2r[0], 0, frame * sizeof(float));
    memset(&o_latl[0], 0, frame * sizeof(float));
    memset(&o_latr[0], 0, frame * sizeof(float));
    nae.fillOutputBuffer(C1_LEFT, &o_c1l[0]);
    nae.fillOutputBuffer(C1_RIGHT, &o_c1r[0]);
    nae.fillOutputBuffer(C2_LEFT, &o_c2l[0]);
    nae.fillOutputBuffer(C2_RIGHT, &o_c2r[0]);
    nae.fillOutputBuffer(LAT_LEFT, &o_latl[0]);
    nae.fillOutputBuffer(LAT_RIGHT, &o_latr[0]);
    for(int i = 0; i < frame; i++) {
      c1.push_back((double)o_c1l[i]);
      c1.push_back((double)o_c1r[i]);
      c2.push_back((double)o_c2l[i]);
      c2.push_back((double)o_c2r[i]);
      lat.push_back((double)o_latl[i]);
      lat.push_back((double)o_latr[i]);
    }

    nae.signal();
    usleep(settle_us);
  }

  sf_close(in);
  return true;
}

static bool write_wav(const char *path, const std::vector<double>& data, int rate)
{
  SF_INFO info;
  memset(&info, 0, sizeof(info));
  info.samplerate = rate;
  info.channels = 2;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  SNDFILE *out = sf_open(path, SFM_WRITE, &info);
  if(out != NULL)
    /* No PEAK chunk. libsndfile writes one for float files and stamps it with
       the time of day, so two byte-identical runs produce two different files
       and a plain cmp reports a difference that is not one. The whole point of
       this tool is that cmp be trustworthy. */
    sf_command(out, SFC_SET_ADD_PEAK_CHUNK, NULL, SF_FALSE);
  if(out == NULL) {
    fprintf(stderr, "nae_bench: cannot write %s: %s\n", path, sf_strerror(NULL));
    return false;
  }
  sf_writef_double(out, &data[0], (sf_count_t)(data.size() / 2));
  sf_close(out);
  return true;
}

int main(int argc, char **argv)
{
  /* --lat writes the lateral pair. Named rather than positional, and removed
     from argv before the positional parsing below. */
  const char *lat_path = NULL;
  double lat_t = NA_NAE_LAT_THRESHOLD_DB, lat_k = NA_NAE_LAT_KNEE_DB;
  std::vector<char*> args;
  args.push_back(argv[0]);
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--lat") == 0 && i + 1 < argc) {
      lat_path = argv[++i];
    } else if(strcmp(argv[i], "--lat-split") == 0 && i + 2 < argc) {
      lat_t = atof(argv[++i]);
      lat_k = atof(argv[++i]);
    } else {
      args.push_back(argv[i]);
    }
  }
  argv = &args[0];
  argc = (int)args.size();

  bool check = (argc > 1 && strcmp(argv[1], "--check") == 0);
  /* Where the optional arguments start. --check takes the input at argv[2] and
     no output paths, so its options begin one earlier than the normal form's. */
  int base = check ? 3 : 4;
  if((check && argc < 3) || (!check && argc < 4)) {
    usage(argv[0]);
    return 1;
  }

  const char *in_path = check ? argv[2] : argv[1];
  int mode      = (argc > base + 0) ? atoi(argv[base + 0]) : 0;
  int frame     = (argc > base + 1) ? atoi(argv[base + 1]) : 256;
  int covsteps  = (argc > base + 2) ? atoi(argv[base + 2]) : 3;
  double pan    = (argc > base + 3) ? atof(argv[base + 3]) : 0.0;
  int settle_us = (argc > base + 4) ? atoi(argv[base + 4]) : 8000;

  /* A zero frame or covsteps is not a degenerate run, it is undefined
     behaviour all the way down: zero-length allocations, a covariance divided
     by N-1 with N zero, and a worker thread looping over nothing. Refuse. */
  if(frame <= 0 || covsteps < 1) {
    fprintf(stderr, "nae_bench: frame_size must be > 0 and covsteps >= 1 "
            "(got %d and %d)\n", frame, covsteps);
    return 1;
  }

  printf("nae_bench: %s  mode %d  frame %d  covsteps %d  pan %.3f  settle %d us\n",
         in_path, mode, frame, covsteps, pan, settle_us);
  printf("nae_bench: lateral split at %.2f dB, knee %.2f dB\n", lat_t, lat_k);

  std::vector<double> c1, c2, lat;
  int rate = 0;
  if(!run_pass(in_path, mode, frame, covsteps, pan, settle_us,
               lat_t, lat_k, c1, c2, lat, &rate))
    return 1;

  if(check) {
    std::vector<double> d1, d2, dlat;
    int rate2 = 0;
    if(!run_pass(in_path, mode, frame, covsteps, pan, settle_us,
                 lat_t, lat_k, d1, d2, dlat, &rate2))
      return 1;
    if(c1.size() != d1.size() || c2.size() != d2.size() ||
       lat.size() != dlat.size()) {
      printf("nae_bench: FAIL, the two passes produced different lengths\n");
      return 2;
    }
    size_t bad1 = 0, bad2 = 0, badl = 0;
    for(size_t i = 0; i < c1.size(); i++) {
      if(c1[i] != d1[i]) bad1++;
      if(c2[i] != d2[i]) bad2++;
      if(lat[i] != dlat[i]) badl++;
    }
    printf("nae_bench: %zu samples per component\n", c1.size() / 2);
    if(bad1 == 0 && bad2 == 0 && badl == 0) {
      printf("nae_bench: PASS, the two passes are bit for bit identical\n");
      return 0;
    }
    printf("nae_bench: FAIL, C1 differs in %zu samples, C2 in %zu, lateral in "
           "%zu. Raise settle_us and try again.\n", bad1, bad2, badl);
    return 2;
  }

  if(!write_wav(argv[2], c1, rate)) return 1;
  if(!write_wav(argv[3], c2, rate)) return 1;
  printf("nae_bench: wrote %s and %s (%zu samples each)\n",
         argv[2], argv[3], c1.size() / 2);
  if(lat_path != NULL) {
    if(!write_wav(lat_path, lat, rate)) return 1;
    printf("nae_bench: wrote %s, the lateral pair\n", lat_path);
  }
  return 0;
}
