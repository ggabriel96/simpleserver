#include <map>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/epoll.h>
using namespace std;

#define GMSG_SIZE 64
#define GBUF_SIZE 1024
#define GMSTIMEOUT 500
#define GMAX_PEER_EV 8
#define GMAX_SERVR_EV 64
#define GMAX_PROD_NAME 64
#define GFGETS_BUF GBUF_SIZE - 1

#define FOREVER for (;;)
#define GREAD_START 0
#define GREAD_NAME 1
#define GREAD_COMMA 2
#define GREAD_SIGN 3
#define GREAD_PRICE 3
#define GREAD_ERR 4

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

typedef struct products_info {
  int amount;
  double total_price;
  products_info(int _amount, double _total_price): amount(_amount), total_price(_total_price) {};
  products_info(): products_info(0, 0.0) {};
} products_info_t;

typedef struct peer_data {
  int fd;
  FILE *file;
  char data[GBUF_SIZE];
  products_info_t info;
  bool done_peer, done_servr;
  ssize_t data_length, data_read;
  peer_data(int _fd, FILE *_file): fd(_fd), file(_file), info(), done_peer(false), done_servr(false), data_length(0), data_read(0) {
    memset(data, 0, sizeof (data));
  };
  peer_data(int _fd): peer_data(_fd, NULL) {};
  peer_data(): peer_data(-1, NULL) {};
} peer_data_t;

off_t fsize(FILE *);
bool alphabet(char);
bool numeric(char);
bool sign(char);

void init_hints(struct addrinfo *);
int init_sfd(const char *, const char *, bool);
int client_sfd(const char *, const char *);
int server_sfd(const char *);
int close_sock(int &);
int close_sock(struct epoll_event &);
int send_eof(struct epoll_event &);
int send_servr(peer_data_t &);
int recv_servr(peer_data_t &);
int send_peer(map<int, peer_data_t>::iterator);
int recv_peer(map<int, peer_data_t>::iterator);

void *server(void *);
int calc_products(peer_data_t &);
