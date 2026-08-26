/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

#ifndef _NA_REMOTE_HPP_
#define _NA_REMOTE_HPP_

extern "C" {

#include <pthread.h>

}

#include <sstream>
#include <string>
#include <vector>

#include "iojack.hpp"

/* Line-based TCP manager for the port gains.
 *
 * Opened only when the configuration carries a <remote> tag naming a port, and
 * bound to the loopback address alone: the commands take no credential of any
 * kind, so anything that can reach the socket can drive the speakers. Reaching
 * it from another machine is a job for an SSH tunnel, which is where the
 * authentication belongs.
 *
 * The protocol is one command per line, meant to be as easy to write from a
 * shell as it is to parse:
 *
 *     up   <dB> <port> [port ...]     raise those ports' gains by <dB>
 *     down <dB> <port> [port ...]     lower them by <dB>
 *     get  [port ...]                 report the gains as they are now
 *     mute                            silence every output, gains untouched
 *     unmute                          bring them back
 *     toggle                          whichever of the two is not the case now
 *
 * The number is signed, so "up -1.0" and "down 1.0" are the same instruction;
 * "down" exists so that a script that computes a positive step reads the way it
 * means. Both are RELATIVE to the gain the port has at that moment, which is
 * what makes them usable from a key binding. Port names are JACK port names as
 * declared in <jack_input> and <jack_output>; inputs and outputs may be mixed
 * in the same line.
 *
 *     echo "up -1.0 front_output_left front_output_right" | nc -w 1 localhost 7000
 *
 * "get" takes no number, and with no names at all it reports every port, which
 * is how a balance arrived at by ear gets written down. While mute is on it
 * says so on a line of its own before the gains, which are then what the ports
 * will go back to rather than what is coming out.
 *
 * "mute" and "unmute" take nothing at all and apply to every output at once.
 * The ports' own gains are untouched, so up/down and get go on working through
 * a mute and the levels come back exactly as they were. "toggle" is the pair of
 * them for a caller with one button: it flips the flag here rather than making
 * the caller read it and decide, and answers with the state it landed on.
 *
 * A command that names a port that does not exist changes and reports NOTHING,
 * as does an up/down that names one twice: half a trimmed speaker pair is worse
 * than an untrimmed one. Every line is answered, one reply line per port on
 * success -- "ok <port> <dB>", the gain the port has once the command is done,
 * the same shape for all three commands -- or a single "error: ..." line, so a
 * caller that reads the socket back knows what happened.
 */
class Remote {

private:

  int port;
  ioJack *naJack;
  bool quiet;

  int listen_fd;
  /* Wakes the accept loop out of poll() at shutdown. A flag alone would not:
     the thread sits blocked until something happens on a descriptor, and the
     something has to be one we can produce on demand. */
  int stop_pipe[2];
  pthread_t thread;
  bool running;

  static void *thread_entry(void *arg);
  void acceptLoop(void);
  void serve(int fd);
  std::string runCommand(const std::string& line);
  std::string reportGains(std::istringstream& is);

public:

  Remote(int n_port, ioJack *n_jack, bool n_quiet);
  ~Remote(void);

  /* Bind, listen and start the manager thread. False (with a message on
     stderr) if the port cannot be opened -- which is worth refusing to start
     for: the usual cause is another natambio already holding it, and carrying
     on would leave this instance running with the commands going elsewhere. */
  bool start(void);

  /* Stop the thread and close the socket. Idempotent, and called by the
     destructor, which must run BEFORE the ioJack it points at is deleted. */
  void stop(void);

};

#endif
