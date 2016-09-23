#include "products.h"
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
