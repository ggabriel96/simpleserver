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
using namespace std;

int main(int argc, const char *argv[]) {
  fdset_t sfd;
  FILE *f = NULL;
  peer_data_t peer;
  sigset_t sigmask;
  ssize_t nread = 0;
  char msg[GMSG_SIZE];
  struct signalfd_siginfo siginfo;
  bool done = false, connected = false;
  const char *port = NULL, *path = NULL;
  int send_servr_st = -1, recv_servr_st = -1;
  struct epoll_event event, events[GMAX_PEER_EV];
  int nevs = 0, n = 0, optval = 0; socklen_t optlen = 0;
  errno = EXIT_SUCCESS;
  memset(msg, 0, sizeof (msg));
  if (argc == 3) {
    port = (char *) "1197";
    path = argv[2];
  } else if (argc == 4) {
    port = argv[2];
    path = argv[3];
  }
  if (port != NULL && path != NULL) {
    f = fopen(path, "r");
    if (f != NULL) {
      sigemptyset(&sigmask);
      sigaddset(&sigmask, SIGHUP); // Hangup detected on controlling terminal or death of controlling process
      sigaddset(&sigmask, SIGINT); // Interrupt from keyboard
      sigaddset(&sigmask, SIGQUIT); // Quit from keyboard
      sigaddset(&sigmask, SIGABRT); // Abort signal from abort(3)
      sigaddset(&sigmask, SIGTERM); // Termination signal
      sigaddset(&sigmask, SIGSTOP); // Stop process
      sigaddset(&sigmask, SIGTSTP); // Stop typed at terminal
      if (sigprocmask(SIG_BLOCK, &sigmask, NULL) == 0) {
        sfd.sign = signalfd(-1, &sigmask, 0);
        if (sfd.sign != -1) {
          sfd.epoll = epoll_create1(0);
          if (sfd.epoll != -1) {
            sfd.peer = client_sfd(argv[1], port);
            if (sfd.peer != -1) {
              event.data.fd = sfd.peer;
              event.events = EVENT_PEER_OUT;
              peer = peer_data_t(sfd.peer);
              peer.file = f;
              if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.peer, &event) != -1) {
                event.data.fd = sfd.sign;
                event.events = EPOLLIN;
                if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.sign, &event) != -1) {
                  while (errno == EXIT_SUCCESS && !done) {
                    nevs = epoll_pwait(sfd.epoll, events, GMAX_PEER_EV, GMSTIMEOUT, NULL);
                    if (nevs != -1) {
                      for (n = 0; n < nevs; n++) {
                        if (events[n].data.fd == sfd.sign) {
                          nread = read(events[n].data.fd, &siginfo, sizeof (struct signalfd_siginfo));
                          if (nread == sizeof (struct signalfd_siginfo)) {
                            if (siginfo.ssi_signo == SIGHUP || siginfo.ssi_signo == SIGINT
                                || siginfo.ssi_signo == SIGQUIT || siginfo.ssi_signo == SIGABRT
                                || siginfo.ssi_signo == SIGTERM || siginfo.ssi_signo == SIGSTOP
                                || siginfo.ssi_signo == SIGTSTP) {
                              printf("\nCancel request received\n");
                              done = true; // quits while loop with !done condition
                              break;
                            } else fprintf(stderr, "# Received signal %u\n", siginfo.ssi_signo);
                          } else {
                            perror("# client main sfd.sign read"); // quits while loop with errno == EXIT_SUCCESS condition
                            break;
                          }
                        }
                        // peer socket file descriptor events
                        if (events[n].events & EVENT_CLOSE) {
                          // Closing the descriptor will make epoll remove it
                          // from the set of descriptors which are monitored.
                          printf("Connection closed\n");
                          close_sock(events[n]);
                          done = true;
                          break;
                        }
                        if (events[n].events & EPOLLERR) {
                          fprintf(stderr, "# EPOLLERR on socket %d. Closing it...\n", events[n].data.fd);
                          if (getsockopt(events[n].data.fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0)
                            fprintf(stderr, "# EPOLLERR info: %s\n", strerror_r(optval, msg, GMSG_SIZE));
                          else fprintf(stderr, "# Could not getsockopt error information on socket %d\n", events[n].data.fd);
                          close_sock(events[n]);
                          done = true;
                          break;
                        } else if (events[n].events & EPOLLOUT) {
                          if (!connected) {
                            if (getsockopt(events[n].data.fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0) {
                              if (optval == 0) {
                                connected = true;
                                printf("Socket connected\n");
                              } else {
                                errno = optval;
                                fprintf(stderr, "# Socket connection failed: %s\n", strerror_r(optval, msg, GMSG_SIZE));
                                break;
                              }
                            } else {
                              perror("# client main getsockopt");
                              break;
                            }
                          } else if (!peer.done_peer) {
                            send_servr_st = send_servr(peer);
                            perror("# client main send_servr");
                            if (send_servr_st == 0) errno = EXIT_SUCCESS;
                            else if (send_servr_st == 1) {
                              peer.done_peer = true;
                              printf("Done sending data to server. Sending 'EOF'...\n");
                              // events[n].events = EVENT_PEER_IN;
                              // if (epoll_ctl(sfd.epoll, EPOLL_CTL_MOD, events[n].data.fd, &events[n]) != -1) {
                                // printf("Successfully switched to EVENT_PEER_IN\n");
                                // errno = EXIT_SUCCESS;
                              // } else perror("# main EPOLL_CTL_MOD EVENT_PEER_IN");
                            }
                          } else if (send_eof(events[n]) != 0) perror("# client main send_eof");
                        } else if (events[n].events & EPOLLIN) {
                          recv_servr_st = recv_servr(peer);
                          perror("# client main recv_servr");
                          if (recv_servr_st == 0) errno = EXIT_SUCCESS;
                          else if (recv_servr_st == 1) {
                            printf("Done receiving data from server. Closing connection...\n");
                            close_sock(events[n]);
                            done = true;
                            break;
                          }
                        }
                      }
                    } else if (errno == EINTR) errno = EXIT_SUCCESS;
                    else perror("# client main epoll_pwait");
                  }
                } else perror("# client main epoll_ctl ADD sfd.sign");
              } else perror("# client main epoll_ctl ADD sfd.peer");
            } else perror("# client main Could not initialize socket");
          } else perror("# client main epoll_create1");
        } else perror("# client main signalfd");
      } else perror("# client main sigprocmask");
      fclose(f); f = NULL;
    } else perror("# Could not open file");
  } else {
    fprintf(stderr, "# Wrong arguments! The correct usage is: %s host [port] file. If no port is specified, it defaults to 1197\n", argv[0]);
    errno = EINVAL;
  }
  for (n = 0; n < GMAX_SERVR_EV; n++)
    close_sock(events[n]);
  // already closed all other sockets on main loop or loop above
  close_sock(sfd.epoll);
  port = NULL;
  path = NULL;
  exit(errno);
}
