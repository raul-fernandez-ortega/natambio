/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 *
 */

extern "C" {

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

}

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "remote.hpp"

/* A command line is a handful of port names at most. The cap is there so that
   a client sending an endless stream with no newline in it cannot grow the
   buffer without bound; it is generous enough that no honest command meets it. */
#define REMOTE_MAX_LINE   4096
/* A connection that goes quiet must not hold the manager: commands are served
   one at a time, so a client that connects and says nothing would otherwise
   keep every other client waiting for as long as it cared to. */
#define REMOTE_IO_TIMEOUT_S  2

/* The whole grammar in one place: both the usage error and the unknown-command
   one quote it, and each answer stays the single line the protocol promises. */
static const char *REMOTE_GRAMMAR =
  "up|down <dB> <port> [port ...] | upin|downin|upout|downout <dB> | get [port ...] | "
  "naegain [<nae> [front|amb|rear <dB> ...]] | naepan [<nae> [<scale>]] | "
  "naeget [<nae>] | getxmlconfig | timecycle [reset] | mute | unmute | toggle";

/* The three NAE gains as the protocol spells them, in the order a report lists
   them. Kept together so the parser and the reporter cannot drift apart. */
static const struct {
  const char *word;
  enum nae_gain which;
} REMOTE_NAE_GAINS[] = {
  { "front", NAE_GAIN_FRONT },
  { "amb",   NAE_GAIN_AMB   },
  { "rear",  NAE_GAIN_REAR  },
};
#define REMOTE_NAE_GAIN_COUNT (sizeof(REMOTE_NAE_GAINS)/sizeof(REMOTE_NAE_GAINS[0]))

/* The next name on the line. An NAE's <name> is free text out of the
   configuration -- "front stereo" is one of the names in use -- so unlike a
   JACK port name it is not always one whitespace-delimited token. Quoted, it
   may hold spaces, and inside the quotes a backslash escapes the character
   after it, which is what lets a name carry a quote or a backslash of its own.
   Unquoted, it is the plain token it looks like. Returns 1 and the name, 0 if
   the line is finished, -1 on a quote that never closes -- which is refused
   rather than read to end of line, since the rest of that line is a gain the
   caller meant for something. */
static int read_name(std::istringstream& is, std::string& out)
{
  out.clear();
  is >> std::ws;
  if(is.eof() || is.peek() == EOF)
    return 0;
  if(is.peek() != '"')
    return (is >> out) ? 1 : 0;

  is.get();                     /* the opening quote */
  int c;
  while((c = is.get()) != EOF) {
    if(c == '\\') {
      int e = is.get();
      if(e == EOF)
        return -1;
      out.push_back((char)e);
      continue;
    }
    if(c == '"')
      return 1;
    out.push_back((char)c);
  }
  return -1;
}

/* A name written the way the protocol reads it: bare when it is a single
   token, quoted when it is not, so that a report can be edited and sent
   straight back as a command instead of having to be quoted by hand. */
static std::string quote_name(const std::string& name)
{
  bool needs = name.empty();
  for(size_t i = 0; i < name.size() && !needs; i++)
    if(isspace((unsigned char)name[i]) || name[i] == '"' || name[i] == '\\')
      needs = true;
  if(!needs)
    return name;
  std::string out = "\"";
  for(size_t i = 0; i < name.size(); i++) {
    if(name[i] == '"' || name[i] == '\\')
      out.push_back('\\');
    out.push_back(name[i]);
  }
  out.push_back('"');
  return out;
}

/* The two headers of a timecycle report, and the placeholder that keeps the
   stage table rectangular. They are the column names and not a sentence, since
   what they are for is to be read once, above the numbers they name. A leading
   "#" rather than "ok": a header is not an answer, and this is how a caller
   tells the two apart -- the same trick "get" plays with its "muted" line, and
   the reason the data lines keep the "ok" they would otherwise have no use for
   in a table. */
#define REMOTE_TIME_PERIOD_HEADER  "#\tperiod_us\tframes\trate\n"
/* The xrun table's three counts, in the order they narrow: every xrun since
   natambio started, those since the last "timecycle reset", and how many of
   them the delay figures cover. No load column -- an xrun is a period that did
   not fit, and a percentage of the period it overran by would be read as a
   share of one, which is the opposite of what it says. */
#define REMOTE_TIME_XRUN_HEADER    \
  "#\txruns\tsince_reset\tn\tmean_us\tsd_us\tmin_us\tmax_us\n"
#define REMOTE_TIME_STAGE_HEADER   \
  "#\tstage\tname\tcycles\tn\tmean_us\tsd_us\tmin_us\tmax_us\tload_pct\n"
/* The name column of a stage that has no name. Not empty: an empty column in
   the middle of a tab-separated line is two tabs together, which reads as a
   missing field rather than as a field with nothing in it. It cannot be
   confused with an engine's name either -- an unnamed engine is reported as
   the empty quoted string, and a named one is quoted the moment it holds
   anything that would need quoting. */
#define REMOTE_TIME_NO_NAME        "-"

/* The numbers of one timer, in the order the stage header names them: the
   cycles timed, how many of them the figures cover, the mean, the deviation,
   the shortest and the longest -- all microseconds -- and the mean as a
   percentage of the period. Tab-separated, like the rest of the line and like
   the header above it, so that the report is a table both to read and to cut
   fields out of. The caller has already set the stream's precision; this
   returns the tail of a line, newline included, so that the three kinds of
   stage line differ only in what comes before it. */
static std::string report_time_stats(const struct na_time_stats& st, double period_us)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  out << st.cycles << "\t" << st.n << "\t"
      << st.mean_us << "\t" << st.sd_us << "\t"
      << st.min_us << "\t" << st.max_us << "\t"
      << ((period_us > 0.0) ? (100.0 * st.mean_us / period_us) : 0.0) << "\n";
  return out.str();
}

static std::string usage_error(void)
{
  return std::string("error: usage: ") + REMOTE_GRAMMAR + "\n";
}

Remote::Remote(int n_port, ioJack *n_jack, NaConf *n_conf, bool n_quiet)
{
  port = n_port;
  naJack = n_jack;
  naConf = n_conf;
  quiet = n_quiet;
  listen_fd = -1;
  stop_pipe[0] = stop_pipe[1] = -1;
  thread = 0;
  running = false;
}

Remote::~Remote(void)
{
  stop();
}

bool Remote::start(void)
{
  struct sockaddr_in addr;
  int on = 1;

  if(port <= 0 || port > 65535) {
    std::cerr << "Remote: invalid control port " << port << "." << std::endl;
    return false;
  }

  if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cerr << "Remote: cannot create the control socket: " << strerror(errno) << std::endl;
    return false;
  }
  /* So that a restart does not have to wait out the TIME_WAIT of the previous
     run's connections before it can bind again. */
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  /* Loopback only. The commands carry no credential, so binding this to the
     network would hand the volume of the system to anyone on it; an SSH tunnel
     is the way to reach it from elsewhere. */
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Remote: cannot bind the control port 127.0.0.1:" << port << ": "
              << strerror(errno) << std::endl;
    close(listen_fd);
    listen_fd = -1;
    return false;
  }
  if(listen(listen_fd, 4) < 0) {
    std::cerr << "Remote: cannot listen on the control port: " << strerror(errno) << std::endl;
    close(listen_fd);
    listen_fd = -1;
    return false;
  }
  if(pipe(stop_pipe) < 0) {
    std::cerr << "Remote: cannot create the shutdown pipe: " << strerror(errno) << std::endl;
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  running = true;
  if(pthread_create(&thread, NULL, Remote::thread_entry, (void *)this) != 0) {
    std::cerr << "Remote: cannot start the control thread: " << strerror(errno) << std::endl;
    running = false;
    close(stop_pipe[0]); close(stop_pipe[1]);
    stop_pipe[0] = stop_pipe[1] = -1;
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  if(!quiet)
    std::cout << "Remote: control port listening on 127.0.0.1:" << port << std::endl;
  return true;
}

void Remote::stop(void)
{
  if(!running)
    return;
  running = false;
  /* One byte is all the accept loop is waiting for. */
  if(stop_pipe[1] >= 0) {
    char b = 'q';
    ssize_t n = write(stop_pipe[1], &b, 1);
    (void)n;
  }
  pthread_join(thread, NULL);
  thread = 0;
  if(stop_pipe[0] >= 0) { close(stop_pipe[0]); stop_pipe[0] = -1; }
  if(stop_pipe[1] >= 0) { close(stop_pipe[1]); stop_pipe[1] = -1; }
  if(listen_fd >= 0) { close(listen_fd); listen_fd = -1; }
  if(!quiet)
    std::cout << "Remote: control port closed." << std::endl;
}

void *Remote::thread_entry(void *arg)
{
  /* The process's signal handlers belong to the main thread: it is the one
     whose loop watches the stop flag and runs the destructors that close the
     JACK client. Blocking them here keeps a SIGINT from being delivered to a
     thread that would only return EINTR from poll() and carry on. */
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGHUP);
  sigaddset(&set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  reinterpret_cast<Remote *>(arg)->acceptLoop();
  return NULL;
}

void Remote::acceptLoop(void)
{
  struct pollfd pfd[2];

  while(true) {
    pfd[0].fd = listen_fd;      pfd[0].events = POLLIN; pfd[0].revents = 0;
    pfd[1].fd = stop_pipe[0];   pfd[1].events = POLLIN; pfd[1].revents = 0;

    if(poll(pfd, 2, -1) < 0) {
      if(errno == EINTR)
        continue;
      std::cerr << "Remote: poll failed: " << strerror(errno) << "; control port giving up."
                << std::endl;
      return;
    }
    if(pfd[1].revents != 0)     /* stop() asked for it */
      return;
    if((pfd[0].revents & POLLIN) == 0)
      continue;

    int fd = accept(listen_fd, NULL, NULL);
    if(fd < 0) {
      if(errno == EINTR || errno == ECONNABORTED || errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
      std::cerr << "Remote: accept failed: " << strerror(errno) << "; control port giving up."
                << std::endl;
      return;
    }
    serve(fd);
    close(fd);
  }
}

/* Write a whole reply, or give up on the connection. MSG_NOSIGNAL because a
   client that has already hung up (nc -w 1 does, promptly) must not take the
   process down with a SIGPIPE. */
static bool write_all(int fd, const std::string& s)
{
  size_t sent = 0;
  while(sent < s.size()) {
    ssize_t n = send(fd, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
    if(n <= 0) {
      if(n < 0 && errno == EINTR)
        continue;
      return false;
    }
    sent += (size_t)n;
  }
  return true;
}

void Remote::serve(int fd)
{
  struct timeval tv;
  char buf[512];
  std::string pending;

  tv.tv_sec = REMOTE_IO_TIMEOUT_S;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  while(true) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if(n < 0 && errno == EINTR)
      continue;
    if(n <= 0)
      break;                    /* client hung up, or went quiet for too long */
    pending.append(buf, (size_t)n);

    size_t nl;
    while((nl = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      std::string reply = runCommand(line);
      if(!reply.empty() && !write_all(fd, reply))
        return;
    }
    if(pending.size() > REMOTE_MAX_LINE) {
      write_all(fd, "error: command too long\n");
      return;
    }
  }
  /* A last command with no newline after it -- echo -n, or a client that
     closes its side as soon as it has written -- still counts. */
  if(!pending.empty()) {
    std::string reply = runCommand(pending);
    if(!reply.empty())
      write_all(fd, reply);
  }
}

/* "get [port ...]": report what the gains are, in the same "ok <port> <dB>"
   shape the up/down replies use, so a caller parses one format either way.
   With no names it reports every port -- which is how a balance found by ear
   gets written down, and how a caller with no copy of the configuration finds
   out what there is to ask about. A name repeated in the list is answered
   twice and harms nothing, so unlike up/down it is not refused: only a name
   that exists nowhere is, and then nothing at all is reported, so an answer is
   either complete or an error. */
std::string Remote::reportGains(std::istringstream& is)
{
  std::vector<std::string> names;
  std::string name;

  while(is >> name)
    names.push_back(name);
  if(names.empty())
    names = naJack->portNames();

  std::vector<double> gains(names.size(), 0.0);
  for(size_t i = 0; i < names.size(); i++)
    if(!naJack->portGain(names[i], &gains[i]))
      return "error: no JACK port named '" + names[i] + "'\n";

  std::ostringstream reply;
  /* Said first, and not as an "ok <port> <dB>" line: while mute is on these
     gains are what the ports will go back to, not what is coming out, and a
     get that did not say so would be reporting a level nobody can hear. A
     caller that filters on "ok " skips the line without being taught to. */
  if(naJack->isMuted())
    reply << "muted\n";
  reply << std::fixed << std::setprecision(3);
  for(size_t i = 0; i < names.size(); i++)
    reply << "ok " << names[i] << " " << gains[i] << "\n";
  return reply.str();
}

/* An engine's whole state, as "naegain <nae>" with no gains after it answers:
   which mode it is in, and then all three gains whether that mode reads them or
   not. Reporting only the two a mode uses would make the reply to a set the
   caller could not read back -- a value set on the third would vanish from the
   report that is supposed to be the record of it. */
std::string Remote::reportNaeGains(const std::vector<std::string>& names)
{
  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);

  for(size_t i = 0; i < names.size(); i++) {
    double db = 0.0;
    bool active = false;
    /* The mode, read off the one gain only beta uses. */
    if(!naJack->naeGain(names[i], NAE_GAIN_REAR, &db, &active))
      return "error: no NAE named '" + names[i] + "'\n";
    std::string shown = quote_name(names[i]);
    reply << "nae " << shown << " " << (active ? "beta" : "alpha") << "\n";
    for(size_t g = 0; g < REMOTE_NAE_GAIN_COUNT; g++) {
      naJack->naeGain(names[i], REMOTE_NAE_GAINS[g].which, &db, &active);
      reply << (active ? "ok " : "inactive ") << shown << " "
            << REMOTE_NAE_GAINS[g].word << " " << db << "\n";
    }
  }
  return reply.str();
}

/* "naegain [<nae> [front|amb|rear <dB> ...]]": the NAE engines' gains, absolute
   and in dB. The whole line is resolved before anything is set, the way up/down
   resolve their port list: these three gains are a balance between components,
   and applying half a line would leave the engine at a balance the caller never
   asked for -- and, unlike a mistyped port name, one they have no record of. */
std::string Remote::naeGains(std::istringstream& is)
{
  std::string nae_name;

  int got = read_name(is, nae_name);
  if(got < 0)
    return "error: a name opened with a quote and never closed it\n";
  if(got == 0)
    return reportNaeGains(naJack->naeNames());

  if(!naJack->naeGain(nae_name, NAE_GAIN_FRONT, NULL, NULL))
    return "error: no NAE named '" + nae_name + "'\n";

  std::vector<enum nae_gain> which;
  std::vector<std::string> words;
  std::vector<double> dbs;
  std::string word, arg;

  while(is >> word) {
    size_t g;
    for(g = 0; g < REMOTE_NAE_GAIN_COUNT; g++)
      if(strcasecmp(word.c_str(), REMOTE_NAE_GAINS[g].word) == 0)
        break;
    if(g == REMOTE_NAE_GAIN_COUNT)
      return "error: '" + word + "' is not one of front, amb, rear\n";
    /* Each name carries its own number: the pairs are what makes a line of
       several gains readable, and a name left dangling at the end of the line
       is a number the caller meant to type and did not. */
    if(!(is >> arg))
      return "error: '" + word + "' takes a gain in dB after it\n";
    char *end = NULL;
    double db = strtod(arg.c_str(), &end);
    if(end == arg.c_str() || *end != '\0' || !std::isfinite(db))
      return "error: '" + arg + "' is not a number of dB\n";
    /* Repeated, the second would silently win. The caller who wrote one twice
       meant it once, and which of the two numbers they meant is not for this
       end to guess. */
    for(size_t j = 0; j < which.size(); j++)
      if(which[j] == REMOTE_NAE_GAINS[g].which)
        return "error: gain '" + word + "' listed twice\n";
    which.push_back(REMOTE_NAE_GAINS[g].which);
    words.push_back(REMOTE_NAE_GAINS[g].word);
    dbs.push_back(db);
  }

  if(which.empty())
    return reportNaeGains(std::vector<std::string>(1, nae_name));

  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);
  std::string shown = quote_name(nae_name);
  for(size_t i = 0; i < which.size(); i++) {
    double now = 0.0;
    bool active = false;
    naJack->setNaeGain(nae_name, which[i], dbs[i], &now, &active);
    if(naConf != NULL)
      naConf->setNaeGainDb(nae_name, which[i], now);
    reply << (active ? "ok " : "inactive ") << shown << " "
          << words[i] << " " << now << "\n";
  }
  return reply.str();
}

/* "naepan [<nae> [<scale>]]": the width of an engine's input pair, set or
   reported. One number and not three, so unlike naegain there is nothing to
   resolve before applying and nothing half-applied if it is wrong. The range is
   the parameter's domain rather than a safety margin -- +1 is mono, and there
   is no width past it -- so a number outside it is refused, as naconf refuses
   one in the file, instead of being quietly clamped to an end the caller did
   not ask for. It is read by both modes, so unlike a gain it is never
   inactive. */
std::string Remote::naePan(std::istringstream& is)
{
  std::string nae_name;
  double scale = 0.0;

  int got = read_name(is, nae_name);
  if(got < 0)
    return "error: a name opened with a quote and never closed it\n";

  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);

  if(got == 0) {
    /* Every engine, as naegain with no name reports every engine's gains. */
    std::vector<std::string> names = naJack->naeNames();
    for(size_t i = 0; i < names.size(); i++) {
      naJack->naePanScale(names[i], &scale);
      reply << "ok " << quote_name(names[i]) << " " << scale << "\n";
    }
    return reply.str();
  }

  if(!naJack->naePanScale(nae_name, &scale))
    return "error: no NAE named '" + nae_name + "'\n";

  std::string arg;
  if(is >> arg) {
    char *end = NULL;
    double n_scale = strtod(arg.c_str(), &end);
    if(end == arg.c_str() || *end != '\0' || !std::isfinite(n_scale))
      return "error: '" + arg + "' is not a width\n";
    if(n_scale < NA_NAE_PAN_MIN || n_scale > NA_NAE_PAN_MAX)
      return "error: a width is between -1 (opposite polarity) and +1 (mono)\n";
    std::string extra;
    if(is >> extra)
      return "error: naepan takes one width and nothing else\n";
    if(!naJack->setNaePanScale(nae_name, n_scale, &scale))
      return "error: no NAE named '" + nae_name + "'\n";
    if(naConf != NULL)
      naConf->setNaePanScale(nae_name, scale);
  }

  reply << "ok " << quote_name(nae_name) << " " << scale << "\n";
  return reply.str();
}

/* Text going inside an element. The names come out of the configuration file
   and are almost always plain, but "almost" is not a guarantee worth writing a
   malformed document on: a report is meant to be pasted back into the XML, and
   one unescaped ampersand would make the file it lands in unparseable. */
static std::string xml_escape(const std::string& text)
{
  std::string out;
  for(size_t i = 0; i < text.size(); i++) {
    if(text[i] == '&')      out += "&amp;";
    else if(text[i] == '<') out += "&lt;";
    else if(text[i] == '>') out += "&gt;";
    else                    out.push_back(text[i]);
  }
  return out;
}

/* One element, or nothing at all when the value is empty: an engine has only
   some of the six outputs, and writing the ones it does not have as empty tags
   would put names naconf reads as "configured, and configured to nothing" into
   a file meant to reproduce this engine. */
static void xml_line(std::ostringstream& out, const char *tag, const std::string& value)
{
  if(value.empty())
    return;
  out << "  <" << tag << ">" << xml_escape(value) << "</" << tag << ">\n";
}

/* "naeget [<nae>]": the engine's configuration as the <nae> block that would
   reproduce it -- the gains as they are NOW, so a balance arrived at over the
   socket comes back as the lines to paste into the file that will start it
   that way next time. The gains the mode does not read are left out rather
   than written at their floor: naconf does not ask beta for a <front_gain>,
   and a block carrying one it never uses would read as a setting that does
   something. "naegain" is where those are still visible.

   The reply is XML and not the protocol's own "ok <thing> <value>" shape, which
   is the point of having a second command rather than more flags on the first:
   what a caller wants here is the text of a configuration, not a value to act
   on. Each block ends at its </nae>, so a client reading one engine knows where
   the answer stops; asking for all of them has a length only the far end knows,
   like a bare "get". */
std::string Remote::reportNaeConfig(struct nae_config& cfg)
{
  std::ostringstream out;

  /* The tag the engine was declared with, so the block can be pasted back into
     a configuration and start the same engine. An <nae_erb> reported as an
     <nae> would start silently as the broadband one. */
  const std::string& tag = cfg.engine.empty() ? std::string("nae") : cfg.engine;
  bool erb = (tag == "nae_erb");

  out << "<" << tag << ">\n";
  xml_line(out, "name", cfg.name);
  out << "  <steps_length>" << cfg.steps_length << "</steps_length>\n";
  out << "  <mode>" << (cfg.mode ? "beta" : "alpha") << "</mode>\n";
  out << std::fixed << std::setprecision(3);
  if(erb) {
    /* The three the ERB engine adds. Written whatever they are, defaults
       included: the point of the block is to reproduce the engine, and a
       default left out is a default that can change under the file. */
    out << "  <cov_window_ms>" << cfg.erb_cov_window_ms << "</cov_window_ms>\n";
    out << "  <delta_erb>" << cfg.erb_delta_erb << "</delta_erb>\n";
    out << "  <band_min_hz>" << cfg.erb_band_min_hz << "</band_min_hz>\n";
  }
  out << "  <pan_scale>" << cfg.pan_scale << "</pan_scale>\n";
  if(cfg.mode) {
    out << "  <rear_gain>" << cfg.rear_gain_db << "</rear_gain>\n";
  } else {
    out << "  <front_gain>" << cfg.front_gain_db << "</front_gain>\n";
    out << "  <ambience_gain>" << cfg.ambience_gain_db << "</ambience_gain>\n";
  }
  xml_line(out, "input_left", cfg.input_left);
  xml_line(out, "input_right", cfg.input_right);
  xml_line(out, "output_left", cfg.output_left);
  xml_line(out, "output_right", cfg.output_right);
  xml_line(out, "front_output_left", cfg.front_output_left);
  xml_line(out, "front_output_right", cfg.front_output_right);
  xml_line(out, "amb_output_left", cfg.amb_output_left);
  xml_line(out, "amb_output_right", cfg.amb_output_right);
  out << "</" << tag << ">\n";
  return out.str();
}

std::string Remote::naeConfigs(std::istringstream& is)
{
  std::string nae_name;
  struct nae_config cfg;

  int got = read_name(is, nae_name);
  if(got < 0)
    return "error: a name opened with a quote and never closed it\n";

  if(got == 0) {
    /* Every engine, by position rather than by name: one configured without a
       <name> is still part of what is running, and this is the report that
       would otherwise be the only place its absence did not show. */
    std::ostringstream out;
    for(size_t i = 0; i < naJack->naeCount(); i++)
      if(naJack->naeConfigAt(i, &cfg))
        out << reportNaeConfig(cfg);
    return out.str();
  }

  std::string extra;
  if(is >> extra)
    return "error: naeget takes one NAE name and nothing else\n";
  if(!naJack->naeConfig(nae_name, &cfg))
    return "error: no NAE named '" + nae_name + "'\n";
  return reportNaeConfig(cfg);
}

/* "getxmlconfig": the whole configuration, as XML, current because every
   command that changes anything has already written its value into it. */
std::string Remote::xmlConfig(std::istringstream& is)
{
  std::string extra;
  if(is >> extra)
    return "error: getxmlconfig takes no arguments\n";
  if(naConf == NULL)
    return "error: no configuration to report\n";
  std::string doc = naConf->dumpConfig();
  if(doc.empty())
    return "error: the configuration could not be written out\n";
  /* xmlDocDumpMemory ends the document with a newline of its own; make sure,
     since a caller reading lines would otherwise be left holding the last one
     with no terminator. */
  if(doc[doc.size()-1] != '\n')
    doc.push_back('\n');
  return doc;
}

/* "timecycle [reset]": where the period goes, as the running mean and
   deviation of the last thousand cycles of each stage that has a history
   (cycletime.hpp).
 *
 * It is a report and not a value to act on, but it keeps the protocol's shape
 * anyway -- one "ok <thing> <numbers>" line per stage -- because unlike naeget,
 * whose answer is a configuration to paste, every line here is a set of numbers
 * a caller will want to pull a field out of. The first line is the period
 * itself, so that the figures below have something to be a fraction of without
 * the caller having to know the configuration; the last field of every stage
 * line is that fraction, as a percentage, since it is what the question
 * "is this machine keeping up" actually asks.
 *
 * Two counts per stage: the cycles it has timed since the last reset, and how
 * many of those the figures cover -- the FIFO's thousand, or fewer just after
 * a reset. The first is worth reporting for the NAE engines in particular: the
 * callback signals them once per period and does not wait, so an engine whose
 * cycle count trails the callback's is one that is not being given the periods,
 * which no mean of the periods it did get would show.
 *
 * And the xruns, in a table between the two: the periods that did not fit at
 * all, which is what the load figures are read in order to predict. The count
 * of them since the process started is the one number here that a "timecycle
 * reset" leaves alone -- a caller resetting to time the next minute is not
 * asking to forget that the last hour dropped four periods, and until now the
 * only record of that was a line on stderr, which is to say a person reading a
 * log afterwards rather than a manager reading a socket. */
std::string Remote::cycleTimes(std::istringstream& is)
{
  std::string arg;
  if(is >> arg) {
    if(strcasecmp(arg.c_str(), "reset") != 0)
      return "error: timecycle takes 'reset' or nothing at all\n";
    std::string extra;
    if(is >> extra)
      return "error: timecycle reset takes no arguments\n";
    naJack->resetTimeStats();
    return "ok timecycle reset\n";
  }

  int rate = naJack->getSampleRate();
  int frames = naJack->getPartSize();
  /* The period, in the same microseconds as everything below it. Zero rate
     means the client is not open yet, which the manager should not be able to
     see; report the period as 0 rather than divide by it, and the load figures
     go to 0 with it. */
  double period_us = (rate > 0) ? (1000000.0 * (double)frames / (double)rate) : 0.0;

  /* Two tables, each under its own header: the period, which is one row of
     three numbers, and then one row per stage. They are separate because they
     are not the same thing measured -- the period is what the machine is given,
     the stages are what it spends -- and folding them into one table would mean
     a row of dashes where the statistics would go. */
  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);
  reply << REMOTE_TIME_PERIOD_HEADER;
  reply << "ok\t" << period_us << "\t" << frames << "\t" << rate << "\n";

  struct na_time_stats st;

  /* The xruns, between the two and in a table of their own, because they are
     neither: not what the machine is given and not a stage of what it spends,
     but the periods it already failed to fit into the first. They come second
     because they are the outcome the stage table below exists to explain --
     what you were given, what you lost, and then where the time went. The
     delays are JACK's own figure for how late the period ran, so a report can
     say a machine dropped four periods by a millisecond each rather than only
     that it dropped four. */
  naJack->xrunTimeStats(&st);
  reply << REMOTE_TIME_XRUN_HEADER;
  reply << "ok\t" << naJack->xrunCount() << "\t" << st.cycles << "\t" << st.n << "\t"
        << st.mean_us << "\t" << st.sd_us << "\t"
        << st.min_us << "\t" << st.max_us << "\n";

  reply << REMOTE_TIME_STAGE_HEADER;

  naJack->cycleTimeStats(&st);
  reply << "ok\ttotal\t" << REMOTE_TIME_NO_NAME << "\t" << report_time_stats(st, period_us);
  naJack->convTimeStats(&st);
  reply << "ok\tconv\t" << REMOTE_TIME_NO_NAME << "\t" << report_time_stats(st, period_us);

  std::string nae_name;
  for(size_t i = 0; naJack->naeTimeStatsAt(i, &st, &nae_name); i++)
    reply << "ok\tnae\t" << quote_name(nae_name) << "\t"
          << report_time_stats(st, period_us);

  return reply.str();
}

std::string Remote::runCommand(const std::string& line)
{
  std::istringstream is(line);
  std::string cmd;

  if(!(is >> cmd))
    return "";                  /* blank line: nothing asked, nothing said */

  if(strcasecmp(cmd.c_str(), "get") == 0)
    return reportGains(is);

  if(strcasecmp(cmd.c_str(), "naegain") == 0)
    return naeGains(is);

  if(strcasecmp(cmd.c_str(), "naepan") == 0)
    return naePan(is);

  if(strcasecmp(cmd.c_str(), "naeget") == 0)
    return naeConfigs(is);

  if(strcasecmp(cmd.c_str(), "getxmlconfig") == 0)
    return xmlConfig(is);

  if(strcasecmp(cmd.c_str(), "timecycle") == 0)
    return cycleTimes(is);

  if(strcasecmp(cmd.c_str(), "mute") == 0 || strcasecmp(cmd.c_str(), "unmute") == 0 ||
     strcasecmp(cmd.c_str(), "toggle") == 0) {
    /* None takes an argument. A name after them is refused rather than
       ignored: someone who writes "mute sub_output_left" means to silence one
       port, and letting that quietly mute everything is the worst way to find
       out it does not. */
    std::string extra;
    if(is >> extra)
      return "error: " + cmd + " takes no arguments; it applies to every output\n";
    /* "toggle" exists for the one caller that has a single button and no room
       for two: it reads the flag and flips it HERE, on the manager thread that
       is the only writer. A client doing the same with a "get" followed by a
       "mute" decides on a state that may have changed between the two lines.
       The reply is the state it landed on -- "ok mute" or "ok unmute", the same
       two answers as ever -- so a button keeps itself in step with one command
       and no query. */
    bool on;
    if(strcasecmp(cmd.c_str(), "toggle") == 0)
      on = !naJack->isMuted();
    else
      on = (strcasecmp(cmd.c_str(), "mute") == 0);
    naJack->setMute(on);
    return on ? "ok mute\n" : "ok unmute\n";
  }

  /* Sign, and where the command gets its ports from: named on the line, or a
     whole direction at once. The four grouped forms exist because "every
     output" and "every input" are the two lists worth having a name for -- one
     is the volume of the system, the other the level it is fed at -- and
     spelling them out port by port is both tedious and a chance to miss one. */
  double sign;
  enum { NAMED, ALL_INPUTS, ALL_OUTPUTS } scope;
  if(strcasecmp(cmd.c_str(), "up") == 0)             { sign =  1.0; scope = NAMED; }
  else if(strcasecmp(cmd.c_str(), "down") == 0)      { sign = -1.0; scope = NAMED; }
  else if(strcasecmp(cmd.c_str(), "upin") == 0)      { sign =  1.0; scope = ALL_INPUTS; }
  else if(strcasecmp(cmd.c_str(), "downin") == 0)    { sign = -1.0; scope = ALL_INPUTS; }
  else if(strcasecmp(cmd.c_str(), "upout") == 0)     { sign =  1.0; scope = ALL_OUTPUTS; }
  else if(strcasecmp(cmd.c_str(), "downout") == 0)   { sign = -1.0; scope = ALL_OUTPUTS; }
  else
    return "error: unknown command '" + cmd + "'; usage: " + REMOTE_GRAMMAR + "\n";

  std::string arg;
  if(!(is >> arg))
    return usage_error();
  char *end = NULL;
  double delta = strtod(arg.c_str(), &end);
  if(end == arg.c_str() || *end != '\0' || !std::isfinite(delta))
    return "error: '" + arg + "' is not a number of dB\n";

  std::vector<std::string> names;
  std::string name;

  if(scope == NAMED) {
    while(is >> name)
      names.push_back(name);
    if(names.empty())
      return usage_error();

    /* Resolve the whole list before touching anything. A command that names one
       port wrongly is a mistake in the command, and applying it to the rest
       would leave a speaker pair or a crossover split by that mistake -- worse
       than doing nothing and saying so. Repeats are refused for the same reason:
       the caller who wrote a name twice meant it once, not twice as loud. */
    for(size_t i = 0; i < names.size(); i++) {
      if(!naJack->portGain(names[i], NULL))
        return "error: no JACK port named '" + names[i] + "'\n";
      for(size_t j = 0; j < i; j++)
        if(names[j] == names[i])
          return "error: port '" + names[i] + "' listed twice\n";
    }
  } else {
    /* The list is the command; a name after it would be a caller who thinks
       this trims that one port, and it does not. */
    if(is >> name)
      return "error: " + cmd + " takes no port names; it applies to every "
             + (scope == ALL_INPUTS ? "input" : "output") + "\n";
    names = (scope == ALL_INPUTS) ? naJack->inputPortNames() : naJack->outputPortNames();
  }

  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);
  for(size_t i = 0; i < names.size(); i++) {
    double now = 0.0;
    naJack->adjustPortGain(names[i], sign * delta, &now);
    /* Into the configuration as well as into the audio, so that the file this
       system would be started from tomorrow says what it is set to today. */
    if(naConf != NULL)
      naConf->setPortGainDb(names[i], now);
    reply << "ok " << names[i] << " " << now << "\n";
  }
  return reply.str();
}
