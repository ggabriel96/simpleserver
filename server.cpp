#include "gsock.h"
#include <vector>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
using namespace std;

#ifndef NTHREADS
#define NTHREADS 2
#endif

int main(int argc, const char *argv[]) {
  char buf[BUF_SIZE];
  pthread_t thread_id;
  const char *port = NULL;
  vector<pthread_t> threads;
  void *retval = malloc(sizeof (int));
  int ssfd = -1, exit_status = EXIT_SUCCESS, nt = -1, status = 0;
  if (argc <= 2) {
    if (argc == 1) port = (char *) "1197";
    else port = argv[1];
    printf("Starting server on port %s.\nSet to create %d threads\n", port, NTHREADS);
    ssfd = server_sfd(port);
    if (ssfd != -1) {
      for (nt = 0; nt < NTHREADS; nt++) {
        if ((status = pthread_create(&thread_id, NULL, server, (void *) &ssfd)) == 0)
          threads.push_back(thread_id);
        else fprintf(stderr, "pthread_create: %s\n", strerror_r(status, buf, BUF_SIZE));
      }
      for (auto& th: threads) {
        pthread_join(th, &retval);
        printf("Thread %lu joined with exit_status %d\n", th, *(int *) retval);
      }
    } else {
      exit_status = EXIT_FAILURE;
      fprintf(stderr, "Could not initialize socket\n");
    }
  } else {
    fprintf(stderr, "# Wrong parameters\n! Correct usage: %s [port]\n! If no port is specified, it defaults to 1197\n", argv[0]);
    exit_status = EXIT_FAILURE;
  }
  retval = NULL;
  if (ssfd != -1) { close(ssfd); ssfd = -1; }
  exit(exit_status);
}
