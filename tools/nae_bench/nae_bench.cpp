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
#include "nae_erb.hpp"

static void usage(const char *me)
{
  fprintf(stderr,
          "usage: %s <in.wav> <out_c1.wav> <out_c2.wav>\n"
          "          [mode 0|1] [frame_size] [covsteps] [pan_scale] [settle_us]\n"
          "       %s --check <in.wav> [mode] [frame_size] [covsteps] [pan] [us]\n"
          "\n"
          "  --erb                 use the per-ERB-band engine (NaeErb)\n"
          "  --cov-ms <ms>         its analysis window.        Default 64\n"
          "  --delta-erb <d>       centre spacing in ERB.      Default 2\n"
          "  --band-min <hz>       width floor in Hz.          Default 125\n"
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
                     double pan_scale, int settle_us, bool erb,
                     double cov_ms, double d_erb, double bmin,
                     std::vector<double>& c1, std::vector<double>& c2,
                     int *samplerate)
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

  /* The engine under test. NaeErb is a NAE, so everything below it here is
     the same call sequence; only the decomposition differs. */
  NAE *engine;
  if(erb) {
    NaeErb *e = new NaeErb("bench", mode);
    e->setCovWindowMs(cov_ms);
    e->setDeltaErb(d_erb);
    e->setBandMinHz(bmin);
    engine = e;
  } else {
    engine = new NAE("bench", mode);
  }
  NAE& nae = *engine;
  nae.setQuiet();
  /* Linear, as the configuration parses them: unity everywhere, so what comes
     out is the decomposition and not a mix of it. */
  nae.setC1Gain(1.0);
  nae.setC2Gain(1.0);
  nae.setC2RearGain(1.0);
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

  c1.clear();
  c2.clear();

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
    nae.fillOutputBuffer(C1_LEFT, &o_c1l[0]);
    nae.fillOutputBuffer(C1_RIGHT, &o_c1r[0]);
    nae.fillOutputBuffer(C2_LEFT, &o_c2l[0]);
    nae.fillOutputBuffer(C2_RIGHT, &o_c2r[0]);
    for(int i = 0; i < frame; i++) {
      c1.push_back((double)o_c1l[i]);
      c1.push_back((double)o_c1r[i]);
      c2.push_back((double)o_c2l[i]);
      c2.push_back((double)o_c2r[i]);
    }

    nae.signal();
    usleep(settle_us);
  }

  sf_close(in);
  delete engine;
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
  /* --erb selects the per-band engine; --cov-ms, --delta-erb and --band-min
     set its three parameters. Named rather than positional: a bare --erb
     followed by the positional mode and frame size would otherwise swallow
     them and build a bank out of a frame size, which is exactly what it did
     the first time. Removed from argv before the positional parsing below. */
  bool erb = false;
  double cov_ms = NA_ERB_COV_WINDOW_MS;
  double d_erb = NA_ERB_DELTA_ERB;
  double bmin = NA_ERB_BAND_MIN_HZ;
  std::vector<char*> args;
  args.push_back(argv[0]);
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--erb") == 0) {
      erb = true;
    } else if(strcmp(argv[i], "--cov-ms") == 0 && i + 1 < argc) {
      cov_ms = atof(argv[++i]);
    } else if(strcmp(argv[i], "--delta-erb") == 0 && i + 1 < argc) {
      d_erb = atof(argv[++i]);
    } else if(strcmp(argv[i], "--band-min") == 0 && i + 1 < argc) {
      bmin = atof(argv[++i]);
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
  if(erb)
    printf("nae_bench: ERB engine, cov_window %.1f ms, delta_erb %.2f, "
           "band_min %.1f Hz\n", cov_ms, d_erb, bmin);

  std::vector<double> c1, c2;
  int rate = 0;
  if(!run_pass(in_path, mode, frame, covsteps, pan, settle_us, erb,
               cov_ms, d_erb, bmin, c1, c2, &rate))
    return 1;

  if(check) {
    std::vector<double> d1, d2;
    int rate2 = 0;
    if(!run_pass(in_path, mode, frame, covsteps, pan, settle_us, erb,
                 cov_ms, d_erb, bmin, d1, d2, &rate2))
      return 1;
    if(c1.size() != d1.size() || c2.size() != d2.size()) {
      printf("nae_bench: FAIL, the two passes produced different lengths\n");
      return 2;
    }
    size_t bad1 = 0, bad2 = 0;
    for(size_t i = 0; i < c1.size(); i++) {
      if(c1[i] != d1[i]) bad1++;
      if(c2[i] != d2[i]) bad2++;
    }
    printf("nae_bench: %zu samples per component\n", c1.size() / 2);
    if(bad1 == 0 && bad2 == 0) {
      printf("nae_bench: PASS, the two passes are bit for bit identical\n");
      return 0;
    }
    printf("nae_bench: FAIL, C1 differs in %zu samples, C2 in %zu. "
           "Raise settle_us and try again.\n", bad1, bad2);
    return 2;
  }

  if(!write_wav(argv[2], c1, rate)) return 1;
  if(!write_wav(argv[3], c2, rate)) return 1;
  printf("nae_bench: wrote %s and %s (%zu samples each)\n",
         argv[2], argv[3], c1.size() / 2);
  return 0;
}
