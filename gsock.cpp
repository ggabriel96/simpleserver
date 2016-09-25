#include "gsock.h"
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

void init_sigaction(struct sigaction *action, void (*func)(int, siginfo_t *, void *)) {
  if (action == NULL) return;
  memset(action, 0, sizeof (struct sigaction));
  action->sa_sigaction = func;
  /*
  SA_NODEFER Do  not prevent the signal from being received from within its own signal handler.  This flag is meaningful only when establishing a signal handler.  SA_NOMASK is an obsolete, nonstandard synonym for this flag.
  SA_RESETHAND Restore  the  signal  action  to the default upon entry to the signal handler.  This flag is meaningful only when establishing a signal handler.  SA_ONESHOT is an obsolete, nonstandard synonym for this flag.
  SA_RESTART Provide behavior compatible with BSD signal semantics by making certain system calls restartable across signals.  This  flag  is meaningful only when establishing a signal handler.  See signal(7) for a discussion of system call restarting.
  SA_SIGINFO (since Linux 2.2) The signal handler takes three arguments, not one.  In this case, sa_sigaction should be set instead of sa_handler.   This flag is meaningful only when establishing a signal handler.
  */
  action->sa_flags = SA_SIGINFO;
}

void init_hints(struct addrinfo *hints) {
  if (hints == NULL) return;
  memset(hints, 0, sizeof (struct addrinfo));
  hints->ai_flags = AI_PASSIVE; // For wildcard IP address, all interfaces
  hints->ai_family = AF_UNSPEC; // IPv4 and IPv6
  hints->ai_socktype = SOCK_STREAM; // TCP socket
  // hints->ai_protocol = IPPROTO_TCP;
}

int client_sfd(const char *host, const char *port) {
  return init_sfd(host, port, false);
}

int server_sfd(const char *port) {
  return init_sfd(NULL, port, true);
}

int init_sfd(const char *host, const char *port, bool is_server) {
  const int yes = 1, flags = is_server ? SOCK_NONBLOCK : 0;
  struct addrinfo hints, *rs = NULL, *r = NULL;
  int sfd = -1;
  init_hints(&hints);
  errno = getaddrinfo(host, port, &hints, &rs);
  if (!errno) {
    for (r = rs; r != NULL; r = r->ai_next) {
      sfd = socket(r->ai_family, r->ai_socktype | flags, r->ai_protocol);
      if (sfd != -1) {
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
          perror("init_sfd setsockopt SO_REUSEADDR");
        // since Linux 3.9:
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes) == -1)
          perror("init_sfd setsockopt SO_REUSEPORT");
        if ((is_server && bind(sfd, r->ai_addr, r->ai_addrlen) == 0)
            || (!is_server && connect(sfd, r->ai_addr, r->ai_addrlen) == 0))
          break; // success
        close(sfd); sfd = -1;
      } else perror("init_sfd socket");
    }
    if (rs != NULL) { freeaddrinfo(rs); rs = NULL; }
    if (r == NULL) perror(is_server ? "init_sfd bind" : "init_sfd connect");
    else r = NULL;
  } else fprintf (stderr, "getaddrinfo: %s\n", gai_strerror(errno));
  return sfd;
}

void *server(void *port) {
  fdset_t sfd;
  sigset_t sigmask;
  bool done = false;
  ssize_t nread = -1;
  char buf[GBUF_SIZE];
  struct sockaddr peer_addr;
  pthread_t self = pthread_self();
  struct signalfd_siginfo siginfo;
  // signal and peer socket file descriptors
  struct epoll_event event, events[GMAX_EVENTS];
  socklen_t peer_addr_len = sizeof (struct sockaddr);
  int nevs = -1, n = -1, optval = -1; socklen_t optlen = 0;
  errno = EXIT_SUCCESS;
  memset(&event, 0, sizeof (event));
  memset(events, 0, sizeof (events));
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGUSR1);
  errno = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
  if (!errno) {
    sfd.sign = signalfd(-1, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd.sign != -1) {
      sfd.epoll = epoll_create1(0);
      if (sfd.epoll != -1) {
        sfd.servr = server_sfd((char *) port);
        if (sfd.servr != -1) {
          if (listen(sfd.servr, SOMAXCONN) != -1) {
            event.data.fd = sfd.servr;
            event.events = EPOLLET | EPOLLIN;
            if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.servr, &event) != -1) {
              event.data.fd = sfd.sign;
              event.events = EPOLLET | EPOLLIN | EPOLLPRI;
              if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.sign, &event) != -1) {
                while (errno == EXIT_SUCCESS && !done) {
                  // last argument is a sigmask while waiting for epoll_pwait
                  nevs = epoll_pwait(sfd.epoll, events, GMAX_EVENTS, GMSTIMEOUT, NULL);
                  if (nevs != -1) {
                    for (n = 0; n < nevs; n++) {
                      if (events[n].data.fd == sfd.sign) {
                        nread = read(events[n].data.fd, &siginfo, sizeof (struct signalfd_siginfo));
                        if (nread == sizeof (struct signalfd_siginfo)) {
                          if (siginfo.ssi_signo == SIGUSR1) {
                            printf("Thread %lu received cancel request!\n", self);
                            done = true; // quits while loop with !done condition
                            break;
                          }
                        } else {
                          perror("data sfd.sign read"); // quits while loop with errno == EXIT_SUCCESS condition
                          break;
                        }
                      }
                      if (events[n].events & EPOLLRDHUP) {
                        printf("\nPeer socket %d on thread %lu has shut down\n", events[n].data.fd, self);
                        close(events[n].data.fd); events[n].data.fd = -1;
                        continue;
                      }
                      if (events[n].events & EPOLLHUP) {
                        printf("\nPeer socket %d on thread %lu has closed the connection\n", events[n].data.fd, self);
                        close(events[n].data.fd); events[n].data.fd = -1;
                        continue;
                      }
                      if (events[n].events & EPOLLERR) {
                        if (getsockopt(events[n].data.fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0)
                          fprintf(stderr, "EPOLLERR event on thread %lu, index %d: %s\n", self, n, strerror_r(optval, buf, GBUF_SIZE));
                        else fprintf(stderr, "EPOLLERR event on thread %lu, index %d (could not getsockopt error information)\n", self, n);
                        close(events[n].data.fd); events[n].data.fd = -1;
                        continue;
                      }
                      if (events[n].data.fd == sfd.servr) {
                        printf("\nIncomming connection on thread %lu...\n", self);
                        sfd.peer = accept4(sfd.servr, &peer_addr, &peer_addr_len, SOCK_NONBLOCK);
                        if (sfd.peer != -1) {
                          printf("Thread %lu accepted socket %d\n", self, sfd.peer);
                          event.data.fd = sfd.peer;
                          event.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                          if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.peer, &event) == -1)
                            fprintf(stderr, "data epoll_ctl sfd.peer on thread %lu: %s\n", self, strerror_r(errno, buf, GBUF_SIZE));
                        } else perror("server accept");
                        errno = EXIT_SUCCESS;
                      } else if (events[n].events & EPOLLIN) {
                        FOREVER {
                          memset(buf, 0, sizeof buf);
                          nread = recv(events[n].data.fd, buf, GBUF_SIZE, 0);
                          if (nread == 0 || (nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                            if (nread == -1) perror("data recv");
                            printf("\nThread %lu closing socket %d...\n", self, events[n].data.fd);
                            // Closing the descriptor will make epoll remove it
                            // from the set of descriptors which are monitored.
                            close(events[n].data.fd); events[n].data.fd = -1;
                            break;
                          }
                          else if (nread > 0) printf("Thread %lu received:\n'%s'\n\n", self, buf);
                        }
                        errno = EXIT_SUCCESS;
                      } else if (events[n].events & EPOLLOUT) {
                        // ready to write on this socket
                      }
                    }
                  } else if (errno == EINTR) errno = EXIT_SUCCESS;
                  else perror("data epoll_pwait");
                }
              } else perror("data epoll_ctl sfd.sign");
            } else perror("connection epoll_ctl sfd.servr");
          } else perror("connection listen");
        } else perror("connection server_sfd");
      } else perror("connection epoll_create1");
    } else perror("data signalfd");
  } else perror("data pthread_sigmask");
  for (n = 0; n < GMAX_EVENTS; n++)
    if (events[n].data.fd > 2)
      { close(events[n].data.fd); events[n].data.fd = -1; }
  // already closed all other sockets on main loop or loop above
  close(sfd.epoll); sfd.epoll = -1;
  pthread_exit(&errno);
}
