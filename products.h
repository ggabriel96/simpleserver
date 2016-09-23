#include <signal.h>

#define BUF_SIZE 512
#define MSTIMEOUT 500
#define MAX_EVENTS 64
#define FOREVER for (;;)
#define MAX_READ_SIZE BUF_SIZE - 1

int init_sfd(const char *, const char *, bool);
int client_sfd(const char *, const char *);
int server_sfd(const char *);
void init_hints(struct addrinfo *);
void init_sigaction(struct sigaction *, void (*)(int, siginfo_t *, void *));
