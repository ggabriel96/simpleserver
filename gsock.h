#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/epoll.h>

#define GMSG_SIZE 64
#define GBUF_SIZE 1024
#define GMSTIMEOUT 500
#define GMAX_PEER_EV 8
#define GMAX_SERVR_EV 64
#define FOREVER for (;;)
#define GFGETS_BUF GBUF_SIZE - 1

const int EVENT_CLOSE = EPOLLRDHUP | EPOLLHUP;
const int EVENT_PEER_OUT = EPOLLOUT | EVENT_CLOSE | EPOLLERR;
const int EVENT_PEER_IN = EPOLLIN | EVENT_CLOSE | EPOLLERR;

typedef struct fdset {
  int sign; // signal socket file descriptor
  int servr; // server socket file descriptor
  int epoll; // epoll socket file descriptor
  int peer; // peer socket file descriptor
  fdset(): sign(-1), servr(-1), epoll(-1), peer(-1) {};
  fdset(int _sign, int _servr, int _epoll, int _peer):
    sign(_sign), servr(_servr), epoll(_epoll), peer(_peer) {};
} fdset_t;

typedef struct peer_data {
  int fd;
  bool done;
  FILE *file;
  size_t data_length;
  char data[GBUF_SIZE];
  peer_data(int _fd, FILE *_file): fd(_fd), done(false), file(_file), data_length(0) {
    memset(data, 0, sizeof (data));
  };
  peer_data(int _fd): peer_data(_fd, NULL) {};
  peer_data(): peer_data(-1, NULL) {};
} peer_data_t;

off_t fsize(FILE *);

void init_hints(struct addrinfo *);
int init_sfd(const char *, const char *, bool);
int client_sfd(const char *, const char *);
int server_sfd(const char *);
int close_sock(int &);
int close_sock(struct epoll_event &);
int send_eof(struct epoll_event &);
int send_servr(peer_data_t &);
int recv_servr(peer_data_t &);
int send_peer(peer_data_t &);
int recv_peer(peer_data_t &);

void *server(void *);
