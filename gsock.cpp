#include "gsock.h"
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>

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

/* From epoll_ctl man page:
EPOLLONESHOT sets the one-shot behavior for the associated file descriptor. This means that after an event is pulled out with epoll_wait(2) the associated file descriptor is internally disabled and no other events will be reported by the epoll interface. The user must call epoll_ctl() with EPOLL_CTL_MOD to rearm the file descriptor with a new event mask. */
void init_epoll_event(struct epoll_event *event, int *sfd) {
  event->data.fd = *sfd;
  event->events = EPOLLET | EPOLLONESHOT | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
}

void term_thread(void *sfd) {
  int *socket = (int *) sfd;
  printf("Socket %d on thread %lu is done.\n", *socket, pthread_self());
}

volatile static sig_atomic_t term = 0;
void int_handl(int sig, siginfo_t *info, void *context) {
  term++;
}

void *server(void *sfd) {
  char buf[BUF_SIZE];
  ssize_t nread = -1;
  int ssfd = *(int *) sfd;
  struct sockaddr peer_addr;
  struct sigaction intaction;
  bool warned = false, done = false;
  int optval = -1; socklen_t optlen = 0;
  struct epoll_event event, events[MAX_EVENTS];
  socklen_t peer_addr_len = sizeof (struct sockaddr);
  // server, epoll and peer socket file descriptors
  int esfd = -1, psfd = -1, nevs = -1, n = -1, exit_status = EXIT_SUCCESS;
  // pthread_cleanup_push(term_thread, (void *) &event->data.fd);
  init_sigaction(&intaction, int_handl);
  // uncomment line below if not willing to interrupt epoll_pwait
  // it adds SIGINT to signal mask, which tells what signals to block
  // sigaddset(&intaction.sa_mask, SIGINT);
  if (sigaction(SIGINT, &intaction, NULL) != -1) {
    if (listen(ssfd, SOMAXCONN) != -1) {
      esfd = epoll_create1(0);
      if (esfd != -1) {
        event.data.fd = ssfd;
        // edge triggered, watch for read operations
        event.events = EPOLLET | EPOLLIN;
        if (epoll_ctl(esfd, EPOLL_CTL_ADD, ssfd, &event) != -1) {
          while (exit_status == EXIT_SUCCESS) {
            if (term == 1) {
              if (!warned) {
                printf("Interrupt request received. Press CTRL+C again within %dms to quit\n", MSTIMEOUT);
                warned = true;
              } else { term = 0; warned = false; }
            } else if (term > 1) break;
            nevs = epoll_pwait(esfd, events, MAX_EVENTS, MSTIMEOUT, &intaction.sa_mask);
            if (nevs != -1) {
              for (n = 0; n < nevs; n++) {
                if (events[n].events & EPOLLRDHUP) {
                  printf("Peer socket %d has shut down\n", events[n].data.fd);
                  close(events[n].data.fd); events[n].data.fd = -1;
                  continue;
                }
                if (events[n].events & EPOLLHUP) {
                  printf("Peer socket %d closed the connection\n", events[n].data.fd);
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
                // ssfd is registered for EPOLLIN... gotta 'else if' down there
                if (events[n].data.fd == ssfd) {
                  printf("Incomming connection...\n");
                  // SOCK_CLOEXEC sets it to automatically close the socket on exec()
                  psfd = accept4(ssfd, &peer_addr, &peer_addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
                  if (psfd != -1) {
                    printf("Accepted socket %d\n", psfd);
                    init_epoll_event(&event, &psfd);
                    if (epoll_ctl(esfd, EPOLL_CTL_ADD, psfd, &event) == -1)
                    fprintf(stderr, "epoll_ctl error: %s\n", strerror_r(errno, buf, BUF_SIZE));
                  } else {
                    exit_status = errno;
                    perror("accept");
                    break;
                  }
                } else if (events[n].events & EPOLLIN) { // I am just watching for EPOLLIN events
                  done = false;
                  FOREVER {
                    memset(buf, 0, sizeof buf);
                    // The only difference between recv and read is the presence of flags.
                    nread = recv(events[n].data.fd, buf, BUF_SIZE, 0);
                    if (nread == 0 || (nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                      done = true;
                      if (nread == -1)
                      fprintf(stderr, "recv error: %s\n", strerror_r(errno, buf, BUF_SIZE));
                    }
                    else if (nread > 0) printf("String received: '%s'\n", buf);
                    if (done) {
                      printf("Done. Closing socket %d...\n", events[n].data.fd);
                      // Closing the descriptor will make epoll remove it from the set of descriptors which are monitored.
                      close(events[n].data.fd); events[n].data.fd = -1;
                      break;
                    }
                  }
                }
              }
            } else if (errno != EINTR) {
              exit_status = errno;
              perror("epoll_pwait");
            }
          }
        } else {
          exit_status = errno;
          perror("epoll_ctl");
        }
      } else {
        exit_status = errno;
        perror("epoll_create1");
      }
    } else {
      exit_status = errno;
      perror("listen");
    }
  } else {
    exit_status = errno;
    perror("sigaction failed to attach signal handler");
  }
  // pthread_cleanup_pop(1);
  pthread_exit(&exit_status);
}
