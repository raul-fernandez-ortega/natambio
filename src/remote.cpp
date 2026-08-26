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

static const char *REMOTE_USAGE =
  "error: usage: up|down <dB> <port> [port ...] | get [port ...]\n";

Remote::Remote(int n_port, ioJack *n_jack, bool n_quiet)
{
  port = n_port;
  naJack = n_jack;
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
  reply << std::fixed << std::setprecision(3);
  for(size_t i = 0; i < names.size(); i++)
    reply << "ok " << names[i] << " " << gains[i] << "\n";
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

  double sign;
  if(strcasecmp(cmd.c_str(), "up") == 0)
    sign = 1.0;
  else if(strcasecmp(cmd.c_str(), "down") == 0)
    sign = -1.0;
  else
    return "error: unknown command '" + cmd + "'; expected up, down or get\n";

  std::string arg;
  if(!(is >> arg))
    return REMOTE_USAGE;
  char *end = NULL;
  double delta = strtod(arg.c_str(), &end);
  if(end == arg.c_str() || *end != '\0' || !std::isfinite(delta))
    return "error: '" + arg + "' is not a number of dB\n";

  std::vector<std::string> names;
  std::string name;
  while(is >> name)
    names.push_back(name);
  if(names.empty())
    return REMOTE_USAGE;

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

  std::ostringstream reply;
  reply << std::fixed << std::setprecision(3);
  for(size_t i = 0; i < names.size(); i++) {
    double now = 0.0;
    naJack->adjustPortGain(names[i], sign * delta, &now);
    reply << "ok " << names[i] << " " << now << "\n";
  }
  return reply.str();
}
