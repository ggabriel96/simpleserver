#include "gsock.h"
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

volatile sig_atomic_t term = 0;

void int_handl(int sig, siginfo_t *info, void *context) {
  term++;
}

int main(int argc, const char *argv[]) {
  char buf[BUF_SIZE];
  const char *port = NULL;
  struct sockaddr peer_addr;
  struct sigaction intaction;
  ssize_t nread = -1, nsent = -1;
  bool warned = false, done = false;
  int optval = -1; socklen_t optlen = 0;
  struct epoll_event event, events[MAX_EVENTS];
  socklen_t peer_addr_len = sizeof (struct sockaddr);
  // server, epoll and peer socket file descriptors
  int ssfd = -1, esfd = -1, psfd = -1, nevs = -1, n = -1, exit_status = EXIT_SUCCESS;

  if (argc <= 2) {
    if (argc == 1) port = (char *) "1197";
    else port = argv[1];
    init_sigaction(&intaction, int_handl);
    // uncomment line below if not willing to interrupt epoll_pwait
    // it adds SIGINT to signal mask, which tells what signals to block
    // sigaddset(&intaction.sa_mask, SIGINT);
    if (sigaction(SIGINT, &intaction, NULL) != -1) {
      ssfd = server_sfd(port);
      if (ssfd != -1) {
        if (listen(ssfd, SOMAXCONN) != -1) {
          printf("Listening on port %s\n", port);
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
                        event.data.fd = psfd;
                        event.events = EPOLLET | EPOLLIN; // EPOLLET | EPOLLIN | EPOLLOUT;
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
              perror("epoll_create1");
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
        exit_status = EXIT_FAILURE;
        fprintf(stderr, "Could not initialize socket\n");
      }
    } else {
      exit_status = errno;
      perror("sigaction failed to attach signal handler");
    }
  } else {
    fprintf(stderr, "# Wrong parameters\n! Correct usage: %s [port]\n! If no port is specified, it defaults to 1197\n", argv[0]);
    exit_status = EXIT_FAILURE;
  }
  if (ssfd != -1) { close(ssfd); ssfd = -1; }
  if (esfd != -1) { close(esfd); esfd = -1; }
  if (psfd != -1) { close(psfd); psfd = -1; }
  exit(exit_status);
}
