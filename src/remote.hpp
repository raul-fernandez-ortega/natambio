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
#include "naconf.hpp"

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
 *     naegain [<nae> [front|amb|rear <dB> ...]]   the NAE engines' gains
 *     naepan  [<nae> [<scale>]]       the width of an NAE's input pair
 *     naeget  [<nae>]                 an NAE's configuration, as XML
 *     getxmlconfig                    the whole live configuration, as XML
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
 *     echo "up -1.0 front_output_left front_output_right" | nc -q0 localhost 7000
 *
 * "-q0" and not "-w 1": the reply is written at once, but this end does not
 * close the connection -- it goes back to recv() for the next line -- so a
 * client waiting for end-of-input waits out its own timeout instead. -q0 sends
 * the command and discards the answer. To read the answer without waiting at
 * all, open the socket from the shell (bash's /dev/tcp) and read exactly the
 * lines the command produces: one per port NAMED, so the count is known before
 * sending. Only a bare "get" and the four grouped forms have a count the caller
 * cannot know in advance, since the ports they answer for are not on the line;
 * mute, unmute and toggle name none either but answer with one line. README.md.
 *
 * "get" takes no number, and with no names at all it reports every port, which
 * is how a balance arrived at by ear gets written down. While mute is on it
 * says so on a line of its own before the gains, which are then what the ports
 * will go back to rather than what is coming out.
 *
 * "naegain" is the odd one: it addresses an NAE engine by its <name> rather
 * than a JACK port, and its numbers are ABSOLUTE dB, not steps. The three gains
 * are a balance between the components the engine decomposes the pair into --
 * <front_gain>, <ambience_gain> and <rear_gain> in the configuration, named
 * front, amb and rear here -- and a balance is set to a value, not nudged from
 * whatever it happens to be. Several in one line, applied together:
 *
 *     naegain nae_front front -1.5 amb -4.0
 *
 * Only some of the three do anything in a given mode: alpha reads front and
 * amb, beta reads rear. A gain the mode does not read is still set and still
 * reported -- the mode does not change while natambio runs, so refusing it
 * would only lose the value -- but it is answered "inactive <nae> <which> <dB>"
 * rather than "ok", which is the difference between a command that did nothing
 * and one that did nothing visible. With no gains after the name the engine is
 * reported and not touched: a "nae <name> alpha|beta" line and then its three
 * gains, four lines. With no name at all, every engine, which is how a caller
 * with no copy of the configuration finds out what there is to ask about.
 *
 * "naepan" is <pan_scale>, the width of the engine's input pair, set or read:
 * +1 the pair collapsed to mono, 0 untouched, -1 the two channels in opposite
 * polarity. Absolute like naegain, and slewed like it too -- the width is a
 * matrix the pair is multiplied by, and applying one whole is as much a click
 * as a jump in a gain. Outside [-1, 1] it is refused rather than clamped: that
 * range is the whole of what the parameter means, not a safety margin. Both
 * modes read it, so it is never inactive.
 *
 * "naeget" answers with the <nae> block that would reproduce the engine as it
 * stands -- the gains as they are NOW, so a balance arrived at over this socket
 * comes back as the lines to paste into the configuration that will start it
 * that way next time. It is the one command whose reply is not of the form
 * "ok <thing> <value>": what is wanted here is the text of a configuration, not
 * a value to act on, and bending it into the protocol's shape would only mean
 * the caller had to bend it back. Each block ends at its </nae>, so a client
 * asking for one engine knows where the answer stops; with no name it reports
 * every engine, whose length only this end knows, as with a bare "get".
 *
 * "getxmlconfig" answers with the entire configuration as it now stands: the
 * document natambio was started from, carrying every change the commands above
 * have made since. It is valid to start another natambio with, because it is
 * the file that started this one -- comments and layout included -- rather than
 * something reconstructed from what was parsed out of it. The way it stays
 * current is that every command that changes something writes the new value
 * back into the configuration as well as into the running audio (NaConf::set*),
 * so there is one place holding what the system is set to and no reconciling to
 * do when someone asks for it. The reply ends at </main>, and its length, like
 * that of a bare "get", is known only at this end.
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
  /* The configuration, so that a change can be written back into it as well as
     applied. NULL is tolerated -- the manager then still drives the audio and
     only getxmlconfig has nothing to say -- so that Remote stays usable in a
     test that has no NaConf to give it. */
  NaConf *naConf;
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
  std::string naeGains(std::istringstream& is);
  std::string reportNaeGains(const std::vector<std::string>& names);
  std::string naePan(std::istringstream& is);
  std::string naeConfigs(std::istringstream& is);
  std::string xmlConfig(std::istringstream& is);
  std::string reportNaeConfig(struct nae_config& cfg);

public:

  Remote(int n_port, ioJack *n_jack, NaConf *n_conf, bool n_quiet);
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
