#include "gsock.h"
#include <map>
#include <set>
#include <string>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
using namespace std;

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
  const int yes = 1;
  int sfd = -1, conn = -1;
  pthread_t self = pthread_self();
  struct addrinfo hints, *rs = NULL, *r = NULL;
  init_hints(&hints);
  errno = getaddrinfo(host, port, &hints, &rs);
  if (!errno) {
    for (r = rs; r != NULL; r = r->ai_next) {
      sfd = socket(r->ai_family, r->ai_socktype | SOCK_NONBLOCK, r->ai_protocol);
      if (sfd != -1) {
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
          perror("# init_sfd setsockopt SO_REUSEADDR");
        // since Linux 3.9:
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes) == -1)
          perror("# init_sfd setsockopt SO_REUSEPORT");
        if (is_server && bind(sfd, r->ai_addr, r->ai_addrlen) == 0) {
          break;
        } else if (!is_server) {
          conn = connect(sfd, r->ai_addr, r->ai_addrlen) == 0;
          if (conn == 0 || (conn == -1 && errno == EINPROGRESS)) {
            errno = EXIT_SUCCESS;
            break;
          }
        }
        close_sock(sfd);
      } else perror("# init_sfd socket");
    }
    if (rs != NULL) { freeaddrinfo(rs); rs = NULL; }
    if (r == NULL) perror(is_server ? "# init_sfd bind" : "# init_sfd connect");
    else r = NULL;
  } else fprintf (stderr, "# init_sfd getaddrinfo on thread %lu: %s\n", self, gai_strerror(errno));
  return sfd;
}

int close_sock(int &socket) {
  int status = 0;
  if (socket > 2) {
    status = close(socket);
    socket = -1;
  }
  return status;
}

int close_sock(struct epoll_event &event) {
  int status = 0;
  if (event.data.fd > 2) {
    status = close(event.data.fd);
    event.data.fd = -1;
  }
  return status;
}

off_t fsize(FILE *f) {
  struct stat st;
  int fd = fileno(f);
  if (fd != -1 && fstat(fd, &st) == 0)
    return st.st_size;
  return -1;
}

int send_eof(struct epoll_event &event) {
  char eof = '\0';
  // if (send(event.data.fd, NULL, 0, 0) == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
  if (send(event.data.fd, &eof, 1, 0) == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    return -1;
  return 0;
}

bool alphabet(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool numeric(char c) {
  return c >= '0' && c <= '9';
}

bool sign(char c) {
  return c == '+' || c == '-';
}

int calc_products(peer_data_t &peer) {
  ssize_t j, n;
  double price = 0.0;
  set<string> products;
  int state = GREAD_START;
  char prod_name[GMAX_PROD_NAME];
  pthread_t self = pthread_self();
  memset(prod_name, 0, sizeof (prod_name));
  for (j = n = 0; peer.data_read < peer.data_length; peer.data_read++) {
    if (state == GREAD_START) {
      if (peer.data[peer.data_read] == ' ') continue;
      if (alphabet(peer.data[peer.data_read])) state = GREAD_NAME;
      else {
        state = GREAD_ERR;
        fprintf(stderr, "# Syntax error on thread %lu, product %d\n", self, peer.info.amount);
        break;
      }
    } else if (state == GREAD_COMMA) {
      if (peer.data[peer.data_read] == ' ') continue;
      if (numeric(peer.data[peer.data_read])) state = GREAD_PRICE;
      else if (sign(peer.data[peer.data_read])) state = GREAD_SIGN;
      else {
        state = GREAD_ERR;
        fprintf(stderr, "# Syntax error on thread %lu, product %d\n", self, peer.info.amount);
        break;
      }
    }
    if (state == GREAD_NAME && peer.data[peer.data_read] == ',') {
      peer.info.amount++;
      if (peer.data_read - j >= GMAX_PROD_NAME) {
        fprintf(stderr, "# Product name %d length on thread %lu overflows %d maximum allowed. Truncating it\n", peer.info.amount, self, GMAX_PROD_NAME);
        n = GMAX_PROD_NAME - 1;
      } else n = peer.data_read - j;
      strncpy(prod_name, peer.data + j, n);
      prod_name[n] = '\0';
      printf("product name: [%s]\n", prod_name);
      products.insert(string(prod_name));
      state = GREAD_COMMA;
      j = peer.data_read + 1;
    } else if (state == GREAD_PRICE && numeric(peer.data[peer.data_read])) {
      state = GREAD_SIGN;
      // and '\0'
    } else if (state == GREAD_SIGN && (alphabet(peer.data[peer.data_read]) || peer.data[peer.data_read] == '\n'))  {
      price = strtod(peer.data + j, NULL);
      printf("price: %lf\n", price);
      peer.info.total_price += price;
      state = GREAD_START;
      j = peer.data_read;
    } else if (state == GREAD_SIGN && sign(peer.data[peer.data_read])) {
      state = GREAD_ERR;
      fprintf(stderr, "# Syntax error on thread %lu, product price %d\n", self, peer.info.amount);
      break;
    }
  }
  if (state == GREAD_ERR) return -1;
  else peer.info.amount = products.size();
  return 0;
}

int send_servr(peer_data_t &peer) {
  int status = 0;
  size_t len = 0;
  ssize_t nsent = 0;
  char buf[GBUF_SIZE];
  long peer_ftell = 0;
  off_t file_length = fsize(peer.file);
  memset(buf, 0, sizeof (buf));
  if (fgets(buf, GFGETS_BUF, peer.file) != NULL) {
    len = strlen(buf); buf[--len] = '\0';
    printf("Attempting to send '%s' (%ld byte%s)... ", buf, len, len == 1 ? "" : "s");
    nsent = send(peer.fd, buf, len, 0);
    if (nsent > 0) {
      printf("success!\n");
      if (nsent + 1 == file_length) status = 1;
    } else if (nsent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      status = -1;
      perror("# send_servr send");
    }
  } else {
    peer_ftell = ftell(peer.file);
    if (peer_ftell != 1) {
      if (peer_ftell == file_length) {
        status = 1;
      } else {
        status = -1;
        errno = EIO;
      }
    } else perror("# send_servr ftell");
  }
  return status;
}

int send_peer(map<int, peer_data_t>::iterator peer_it) {
  string answ;
  int status = 0;
  ssize_t nsent = 0;
  char msg[GMSG_SIZE];
  pthread_t self = pthread_self();
  memset(msg, 0, sizeof (msg));
  if (!peer_it->second.done_servr) {
    status = calc_products(peer_it->second);
    if (status == 0) {
      answ = to_string(peer_it->second.info.amount);
      answ.append(" products and $");
      answ.append(to_string(peer_it->second.info.total_price));
      answ.append(" total price");
      peer_it->second.done_servr = true;
      strcpy(peer_it->second.data, answ.c_str());
      peer_it->second.data_length = answ.length();
    }
  }
  // do not add 'else' here, because block above changes peer.done_servr value!
  if (peer_it->second.done_servr) {
    printf("Thread %lu attempting to send:\n%s\nto socket %d (%ld byte%s)...\n", self, peer_it->second.data, peer_it->second.fd, peer_it->second.data_length, peer_it->second.data_length == 1 ? "" : "s");
    nsent = send(peer_it->second.fd, peer_it->second.data, peer_it->second.data_length, 0);
    if (nsent > 0) {
      printf("Thread %lu successfully returned data to socket %d\n", self, peer_it->second.fd);
      if (nsent == peer_it->second.data_length) status = 1; // doesn't work
      else status = 0;
    } else if (nsent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      status = -1;
      fprintf(stderr, "# send on thread %lu: %s\n", self, strerror_r(errno, msg, GMSG_SIZE));
    }
  } else status = -1;
  return status;
}

int recv_servr(peer_data_t &peer) {
  int status = 0;
  ssize_t nread = 0;
  char buf[GBUF_SIZE];
  errno = EXIT_SUCCESS;
  memset(buf, 0, sizeof (buf));
  nread = recv(peer.fd, buf, GBUF_SIZE, 0);
  if (nread > 0) {
    if (buf[0] == '\0') status = 1;
    else if (peer.data_length + nread < GBUF_SIZE) {
      strncpy(peer.data + peer.data_length, buf, nread);
      peer.data_length += nread;
      peer.data[peer.data_length] = '\0';
    } else {
      // disk, clear peer.file
    }
    printf("recv_servr received:\n%s (%ld byte%s)\n", buf[0] == '\0' ? "'EOF'" : buf, nread, nread == 1 ? "" : "s");
  } else if (nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    status = -1; perror("# recv_servr recv");
  }
  return status;
}

int recv_peer(map<int, peer_data_t>::iterator peer_it) {
  int status = 0;
  ssize_t nread = 0;
  pthread_t self = pthread_self();
  char buf[GBUF_SIZE], msg[GMSG_SIZE];
  errno = EXIT_SUCCESS;
  memset(buf, 0, sizeof (buf));
  memset(msg, 0, sizeof (msg));
  nread = recv(peer_it->second.fd, buf, GBUF_SIZE, 0);
  if (nread > 0) {
    if (buf[0] == '\0') status = 1;
    else if (peer_it->second.data_length + nread < GBUF_SIZE) {
      strncpy(peer_it->second.data + peer_it->second.data_length, buf, nread);
      peer_it->second.data_length += nread;
      peer_it->second.data[peer_it->second.data_length] = '\0';
    } else {
      // disk
    }
    printf("Thread %lu received on socket %d:\n%s (%ld byte%s)\n", self, peer_it->second.fd, buf[0] == '\0' ? "'EOF'" : buf, nread, nread == 1 ? "" : "s");
    printf("\n\nComplete data: %s\n", peer_it->second.data);
  } else if (nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    status = -1;
    fprintf(stderr, "# data recv on thread %lu: %s\n", self, strerror_r(errno, msg, GMSG_SIZE));
  }
  return status;
}

void *server(void *port) {
  fdset_t sfd;
  sigset_t sigmask;
  bool done = false;
  ssize_t nread = 0;
  char msg[GMSG_SIZE];
  struct sockaddr peer_addr;
  map<int, peer_data_t> dataset;
  pthread_t self = pthread_self();
  struct signalfd_siginfo siginfo;
  // signal and peer socket file descriptors
  struct epoll_event event, events[GMAX_SERVR_EV];
  socklen_t peer_addr_len = sizeof (struct sockaddr);
  int nevs = 0, n = 0, optval = 0; socklen_t optlen = 0;
  errno = EXIT_SUCCESS;
  memset(msg, 0, sizeof (msg));
  memset(&event, 0, sizeof (event));
  memset(events, 0, sizeof (events));
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGUSR1);
  errno = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
  if (!errno) {
    sfd.sign = signalfd(-1, &sigmask, SFD_NONBLOCK);
    if (sfd.sign != -1) {
      sfd.epoll = epoll_create1(0);
      if (sfd.epoll != -1) {
        sfd.servr = server_sfd((char *) port);
        if (sfd.servr != -1) {
          if (listen(sfd.servr, SOMAXCONN) != -1) {
            event.data.fd = sfd.servr;
            event.events = EPOLLIN;
            if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.servr, &event) != -1) {
              event.data.fd = sfd.sign;
              event.events = EPOLLIN;
              if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.sign, &event) != -1) {
                while (errno == EXIT_SUCCESS && !done) {
                  // last argument is a sigmask while waiting for epoll_pwait
                  nevs = epoll_pwait(sfd.epoll, events, GMAX_SERVR_EV, GMSTIMEOUT, NULL);
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
                          perror("server sfd.sign read"); // quits while loop with errno == EXIT_SUCCESS condition
                          break;
                        }
                      }
                      if (events[n].events & EVENT_CLOSE) {
                        // Closing the descriptor will make epoll remove it
                        // from the set of descriptors which are monitored.
                        printf("Socket %d on thread %lu has disconnected. Closing it...\n", events[n].data.fd, self);
                        close_sock(events[n]);
                        continue;
                      }
                      if (events[n].events & EPOLLERR) {
                        fprintf(stderr, "# EPOLLERR on thread %lu, socket %d. Closing it...\n", self, events[n].data.fd);
                        if (getsockopt(events[n].data.fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == 0)
                          fprintf(stderr, "# EPOLLERR info: %s\n", strerror_r(optval, msg, GMSG_SIZE));
                        else fprintf(stderr, "# Thread %lu could not getsockopt error information on socket %d\n", self, events[n].data.fd);
                        close_sock(events[n]);
                        continue;
                      }
                      if (events[n].data.fd == sfd.servr) {
                        printf("Incomming connection on thread %lu...\n", self);
                        sfd.peer = accept4(sfd.servr, &peer_addr, &peer_addr_len, SOCK_NONBLOCK);
                        if (sfd.peer != -1) {
                          printf("Thread %lu accepted socket %d\n", self, sfd.peer);
                          event.data.fd = sfd.peer;
                          // the client starts with EVENT_PEER_OUT (sending data to server)
                          // and automatically switches to EVENT_PEER_IN (receiving data from server)
                          // so there is no need to initialize it as only EVENT_PEER_IN
                          event.events = EVENT_PEER_IN;
                          if (epoll_ctl(sfd.epoll, EPOLL_CTL_ADD, sfd.peer, &event) == 0) {
                            dataset[event.data.fd] = peer_data_t(event.data.fd);
                          } else {
                            fprintf(stderr, "# EPOLL_CTL_ADD(peer.sfd %d) on thread %lu: %s. Closing it...\n",  event.data.fd, self, strerror_r(errno, msg, GMSG_SIZE));
                            close_sock(event);
                          }
                        } else perror("# server accept");
                        errno = EXIT_SUCCESS;
                      } else if (events[n].events & EPOLLIN) {
                        auto peer = dataset.find(events[n].data.fd);
                        if (peer != dataset.end()) {
                          if (recv_peer(peer) == 1) {
                            printf("Thread %lu is done receiving data from socket %d....\n", self, events[n].data.fd);
                            events[n].events = EVENT_PEER_OUT;
                            if (epoll_ctl(sfd.epoll, EPOLL_CTL_MOD, events[n].data.fd, &events[n]) != -1) {
                              printf("Thread %lu successfully switched socket %d to EVENT_PEER_OUT\n", self, events[n].data.fd);
                            } else {
                              perror("# server EPOLL_CTL_MOD EVENT_PEER_OUT");
                              close_sock(events[n]);
                            }
                          } else perror("# server recv_peer");
                        } else {
                          fprintf(stderr, "# Thread %lu could not find socket %d on dataset. Closing it...\n", self, event.data.fd);
                          close_sock(events[n]);
                        }
                        errno = EXIT_SUCCESS;
                      } else if (events[n].events & EPOLLOUT) {
                        auto peer = dataset.find(events[n].data.fd);
                        if (peer != dataset.end()) {
                          if (!peer->second.done_peer) {
                            if (send_peer(peer) == 1) {
                              peer->second.done_peer = true;
                              printf("Thread %lu is done sending data to peer on socket %d. Sending 'EOF'...\n", self, events[n].data.fd);
                            } else perror("# server send_peer");
                          } else if (send_eof(events[n]) != 0) perror("# server send_eof");
                        } else {
                          fprintf(stderr, "# Thread %lu could not find socket %d on dataset. Closing it...\n", self, event.data.fd);
                          close_sock(events[n]);
                        }
                        errno = EXIT_SUCCESS;
                      }
                    }
                  } else if (errno == EINTR) errno = EXIT_SUCCESS;
                  else perror("# server epoll_pwait");
                }
              } else perror("# server epoll_ctl sfd.sign");
            } else perror("# server epoll_ctl sfd.servr");
          } else perror("# server listen");
        } else perror("# server server_sfd");
      } else perror("# server epoll_create1");
    } else perror("# server signalfd");
  } else perror("# server pthread_sigmask");
  for (n = 0; n < GMAX_SERVR_EV; n++)
    close_sock(events[n]);
  // already closed all other sockets on main loop or loop above
  close_sock(sfd.epoll);
  pthread_exit(&errno);
}
