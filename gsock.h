#include <signal.h>

#define GBUF_SIZE 1024
#define GMSTIMEOUT 500
#define GMAX_EVENTS 64
#define FOREVER for (;;)
#define GMAX_READ_SIZE GBUF_SIZE - 1

typedef struct fdset {
  int sign; // signal socket file descriptor
  int servr; // server socket file descriptor
  int epoll; // epoll socket file descriptor
  int peer; // peer socket file descriptor
  fdset(): sign(-1), servr(-1), epoll(-1), peer(-1) {};
  fdset(int _sign, int _servr, int _epoll, int _peer):
    sign(_sign), servr(_servr), epoll(_epoll), peer(_peer) {};
} fdset_t;

int init_sfd(const char *, const char *, bool);
int client_sfd(const char *, const char *);
int server_sfd(const char *);
void init_hints(struct addrinfo *);
void init_sigaction(struct sigaction *, void (*)(int, siginfo_t *, void *));
void *server(void *);
