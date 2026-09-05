/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#ifndef _NA_CYCLETIME_HPP_
#define _NA_CYCLETIME_HPP_

extern "C" {

#include <time.h>

}

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

/* How many cycles the FIFO keeps. At 48 kHz and a 64-frame period a thousand
   cycles are 1.3 seconds of history, at 1024 frames 21 seconds: long enough
   that a figure taken from it describes the load rather than the moment, short
   enough that it is still about what the system is doing now. The buffer is
   part of the timer, so this is also what every timer costs -- 4 KB each. */
#define NA_TIME_HISTORY 1000

/* One timer's history, reduced. The times are microseconds because that is the
   scale a JACK period is measured on: 1333 us at 48 kHz and 64 frames, and a
   stage that takes tens of them. cycles is every cycle timed since the last
   reset and n only the ones still in the FIFO, so a caller can tell a mean
   over a full window from one over the first few cycles after a reset. */
struct na_time_stats {
  unsigned long long cycles;
  size_t n;
  double mean_us;
  double sd_us;
  double min_us;
  double max_us;
};

/* A stage's elapsed time, cycle by cycle, written by the thread that runs the
   stage and read by whoever asks.
 *
 * The writer is a real-time thread, so the write path allocates nothing, takes
 * no lock and calls nothing that can block: two clock_gettime() -- vDSO reads,
 * a few tens of nanoseconds each -- and two relaxed stores into a fixed array.
 * That is why the history is a fixed-size ring and not a container: the size
 * is decided here, at compile time, and never again.
 *
 * Reading is deliberately unsynchronised. stats() copies the ring while the
 * timed thread may be writing into it, so a snapshot can mix a fresh sample
 * with older ones -- which is to say the mean may be computed over a window
 * that moved by a cycle or two while it was being read. Locking against that
 * would put a lock in the RT path to protect a figure that is a statistic over
 * a thousand samples; the race is worth more than the lock is.
 *
 * A timer measured in one go is begin() ... end(). A stage that runs in pieces
 * with other work in between -- the convolution, which the callback interrupts
 * to fill the output ports -- uses begin() ... accumulate() around each piece
 * and commit() once, so the cycle contributes one sample made of all of them.
 * A duration this class did not measure -- the delay JACK reports with an xrun
 * -- goes in through push(), which is the same history without the clock.
 */
class CycleTimer {

private:

  /* Nanoseconds per cycle, saturating at 4.29 s: an unsigned int is stored and
     loaded atomically on every machine natambio builds for, and a period that
     took longer than four seconds has stopped being a timing question. */
  std::atomic<unsigned int> ring[NA_TIME_HISTORY];
  std::atomic<unsigned long long> count;

  /* Both belong to the timed thread alone and are never read from outside:
     where the interval being timed started, and what this cycle has gathered
     so far. */
  uint64_t t0;
  uint64_t acc;

public:

  CycleTimer(void)
  {
    t0 = 0;
    acc = 0;
    reset();
  }

  /* CLOCK_MONOTONIC because this measures durations: it does not jump when the
     clock is set, and it is the one clock_gettime() serves from the vDSO
     without entering the kernel, which is what makes it usable in the RT
     callback at all. */
  static uint64_t now_ns(void)
  {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  }

  void begin(void) { t0 = now_ns(); }

  /* Close the piece opened by the last begin() and add it to this cycle. */
  void accumulate(void) { acc += now_ns() - t0; }

  /* This cycle is over: one sample onto the FIFO, and start the next at zero. */
  void commit(void)
  {
    push(acc);
    acc = 0;
  }

  void end(void)
  {
    accumulate();
    commit();
  }

  /* One sample, straight in. The entry point for a duration measured
     elsewhere, and what commit() is built on; count is the number of samples
     ever pushed, so a caller reading it back gets every event and not just the
     ones still in the ring. */
  void push(uint64_t ns)
  {
    unsigned int v = (ns > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned int)ns;
    unsigned long long c = count.load(std::memory_order_relaxed);
    ring[c % NA_TIME_HISTORY].store(v, std::memory_order_relaxed);
    count.store(c + 1, std::memory_order_relaxed);
  }

  /* Forget the history and start counting again. Called from outside the timed
     thread, and racing with it no more than stats() does: at worst the cycle
     being timed as this runs lands in the cleared ring and is counted, which is
     the cycle the caller asked to start measuring from anyway. */
  void reset(void)
  {
    for(size_t i = 0; i < NA_TIME_HISTORY; i++)
      ring[i].store(0, std::memory_order_relaxed);
    count.store(0, std::memory_order_relaxed);
  }

  /* The history, reduced. Two passes over a local copy rather than one over
     sums of squares: the samples are a thousand numbers a few thousand wide,
     and a one-pass variance of those subtracts two large nearly equal figures
     to get a small one. This runs in the manager thread, where the second pass
     costs nothing worth saving. The deviation is the sample one (n-1), zero
     for a single sample, as a deviation of one measurement is. */
  void stats(struct na_time_stats *st) const
  {
    unsigned long long c = count.load(std::memory_order_relaxed);
    size_t n = (c < (unsigned long long)NA_TIME_HISTORY) ? (size_t)c : (size_t)NA_TIME_HISTORY;

    st->cycles = c;
    st->n = n;
    st->mean_us = st->sd_us = st->min_us = st->max_us = 0.0;
    if(n == 0)
      return;

    double us[NA_TIME_HISTORY];
    double sum = 0.0;
    double lo = 0.0, hi = 0.0;
    for(size_t i = 0; i < n; i++) {
      us[i] = (double)ring[i].load(std::memory_order_relaxed) / 1000.0;
      sum += us[i];
      if(i == 0 || us[i] < lo) lo = us[i];
      if(i == 0 || us[i] > hi) hi = us[i];
    }
    double mean = sum / (double)n;
    double var = 0.0;
    for(size_t i = 0; i < n; i++) {
      double d = us[i] - mean;
      var += d * d;
    }
    st->mean_us = mean;
    st->sd_us = (n > 1) ? sqrt(var / (double)(n - 1)) : 0.0;
    st->min_us = lo;
    st->max_us = hi;
  }

};

#endif
