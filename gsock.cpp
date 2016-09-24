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
  struct addrinfo hints, *rs = NULL, *r = NULL;
  int sfd = -1, status = -1, yes = 1, flags = is_server ? SOCK_NONBLOCK : 0;
  init_hints(&hints);
  status = getaddrinfo(host, port, &hints, &rs);
  if (status == 0) {
    for (r = rs; r != NULL; r = r->ai_next) {
      sfd = socket(r->ai_family, r->ai_socktype | flags, r->ai_protocol);
      if (sfd != -1) {
        if ((is_server && bind(sfd, r->ai_addr, r->ai_addrlen) == 0) || (!is_server && connect(sfd, r->ai_addr, r->ai_addrlen) == 0))
        break; // success
        close(sfd); sfd = -1;
      } else fprintf (stderr, "socket: %s\n", gai_strerror(status));
    }
    if (rs != NULL) freeaddrinfo(rs);
    if (r == NULL) perror(is_server ? "bind" : "connect");
    // lose the "Address already in use" error message
    if (sfd != -1 && setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
    fprintf(stderr, "Could not setsockopt SO_REUSEADDR\n");
  } else fprintf (stderr, "getaddrinfo: %s\n", gai_strerror(status));
  return sfd;
}

void *server(void *sfd) {
  sigset_t sigmask;
  bool done = false;
  char buf[BUF_SIZE];
  ssize_t nread = -1;
  int ssfd = *(int *) sfd;
  struct sockaddr peer_addr;
  struct signalfd_siginfo fdsi;
  pthread_t self = pthread_self();
  int optval = -1; socklen_t optlen = 0;
  struct epoll_event event, events[MAX_EVENTS];
  socklen_t peer_addr_len = sizeof (struct sockaddr);
  // signal, epoll and peer socket file descriptors
  int sigfd = -1, esfd = -1, psfd = -1, nevs = -1, n = -1;
  errno = EXIT_SUCCESS;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGUSR1);
  errno = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
  if (!errno) {
    if (listen(ssfd, SOMAXCONN) != -1) {
      sigfd = signalfd(-1, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC);
      if (sigfd != -1) {
        esfd = epoll_create1(0);
        if (esfd != -1) {
          event.data.fd = sigfd;
          event.events = EPOLLET | EPOLLIN | EPOLLPRI;
          if (epoll_ctl(esfd, EPOLL_CTL_ADD, sigfd, &event) != -1) {
            event.data.fd = ssfd;
            // edge triggered (level triggered is the default), watch for read operations
            event.events = EPOLLET | EPOLLIN;
            if (epoll_ctl(esfd, EPOLL_CTL_ADD, ssfd, &event) != -1) {
              while (errno == EXIT_SUCCESS && !done) {
                nevs = epoll_pwait(esfd, events, MAX_EVENTS, MSTIMEOUT, NULL);
                if (nevs != -1) {
                  for (n = 0; n < nevs; n++) {
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
                        fprintf(stderr, "epoll_pwait error event: %s\n", strerror_r(optval, buf, BUF_SIZE));
                      else fprintf(stderr, "epoll_pwait error event (could not getsockopt error information)\n");
                      close(events[n].data.fd); events[n].data.fd = -1;
                      continue;
                    }
                    if (events[n].data.fd == sigfd) {
                      nread = read(events[n].data.fd, &fdsi, sizeof (struct signalfd_siginfo));
                      if (nread == sizeof (struct signalfd_siginfo)) {
                        if (fdsi.ssi_signo == SIGUSR1) {
                          printf("Thread %lu received cancel request!\n", self);
                          done = true; // quits while loop with !done condition
                          break;
                        }
                      } else {
                        perror("sigfd recv"); // quits while loop with errno == EXIT_SUCCESS
                        break;
                      }
                    } else if (events[n].data.fd == ssfd) {
                      printf("\nIncomming connection on thread %lu...\n", self);
                      // SOCK_CLOEXEC sets it to automatically close the socket on exec()
                      psfd = accept4(ssfd, &peer_addr, &peer_addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
                      if (psfd != -1) {
                        printf("Accepted socket %d on thread %lu\n", psfd, self);
                        event.data.fd = psfd;
                        event.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                        if (epoll_ctl(esfd, EPOLL_CTL_ADD, psfd, &event) == -1)
                          fprintf(stderr, "epoll_ctl error: %s\n", strerror_r(errno, buf, BUF_SIZE));
                      } else perror("accept");
                      errno = EXIT_SUCCESS;
                    } else if (events[n].events & EPOLLIN) {
                      FOREVER {
                        memset(buf, 0, sizeof buf);
                        nread = recv(events[n].data.fd, buf, BUF_SIZE, 0);
                        if (nread == 0 || (nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                          if (nread == -1) perror("recv");
                          printf("\nThread %lu closing socket %d...\n", self, events[n].data.fd);
                          // Closing the descriptor will make epoll remove it
                          // from the set of descriptors which are monitored.
                          close(events[n].data.fd); events[n].data.fd = -1;
                          break;
                        }
                        else if (nread > 0) printf("Thread %lu received:\n'%s'\n\n", self, buf);
                      }
                      errno = EXIT_SUCCESS;
                    }
                  }
                } else if (errno == EINTR) errno = EXIT_SUCCESS;
                else perror("epoll_pwait");
              }
            } else perror("ssfd epoll_ctl");
          } else perror("sigfd epoll_ctl");
        } else perror("epoll_create1");
      } else perror("signalfd");
    } else perror("listen");
  } // else perror("pthread_sigmask");
  for (n = 0; n < MAX_EVENTS; n++) {
    close(events[n].data.fd);
    events[n].data.fd = -1;
  }
  // close(psfd); psfd = -1; // already closed on main loop or loop above
  close(esfd); esfd = -1;
  close(sigfd); sigfd = -1;
  pthread_exit(&errno);
}
