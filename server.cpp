#include "gsock.h"
#include <vector>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/signalfd.h>
using namespace std;

#ifndef NT
#define NT 2
#endif

int main(int argc, const char *argv[]) {
  sigset_t sigmask;
  union sigval sigv;
  ssize_t nread = -1;
  pthread_t thread_id;
  vector<pthread_t> threads;
  struct signalfd_siginfo siginfo;
  int sigfd = -1, nt = -1, *retval = NULL;
  const char *port = NULL, *msg = (char *) "Server execution terminated";
  errno = EXIT_SUCCESS;
  if (argc <= 2) {
    if (argc == 1) port = (char *) "1197";
    else port = argv[1];
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGHUP); // Hangup detected on controlling terminal or death of controlling process
    sigaddset(&sigmask, SIGINT); // Interrupt from keyboard
    sigaddset(&sigmask, SIGQUIT); // Quit from keyboard
    sigaddset(&sigmask, SIGABRT); // Abort signal from abort(3)
    sigaddset(&sigmask, SIGTERM); // Termination signal
    sigaddset(&sigmask, SIGSTOP); // Stop process
    sigaddset(&sigmask, SIGTSTP); // Stop typed at terminal
    errno = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (!errno) {
      sigfd = signalfd(-1, &sigmask, 0);
      if (sigfd != -1) {
        printf("Starting server on port %s and %d threads\n", port, NT);
        for (nt = 0; nt < NT; nt++)
          // second argument, currently NULL, is to set as detached thread
          if ((errno = pthread_create(&thread_id, NULL, server, (void *) port)) == 0)
            threads.push_back(thread_id);
        if (!errno || !threads.empty()) {
          printf("Sucessfully created %lu threads\n", threads.size());
          while (errno == EXIT_SUCCESS) {
            nread = read(sigfd, &siginfo, sizeof (struct signalfd_siginfo));
            if (nread == sizeof (struct signalfd_siginfo)) {
              if (siginfo.ssi_signo == SIGHUP || siginfo.ssi_signo == SIGINT
                  || siginfo.ssi_signo == SIGQUIT || siginfo.ssi_signo == SIGABRT
                  || siginfo.ssi_signo == SIGTERM || siginfo.ssi_signo == SIGSTOP
                  || siginfo.ssi_signo == SIGTSTP) {
                sigv.sival_int = siginfo.ssi_signo;
                for (auto& th: threads) {
                  if ((errno = pthread_sigqueue(th, SIGUSR1, sigv)) == 0) {
                    printf("\nCancel request queued for thread %lu...\n", th);
                    errno = pthread_join(th, (void **) &retval);
                    printf("Thread %lu terminated with status %d\n", th, *retval);
                    perror("pthread_join");
                  } else perror("pthread_sigqueue");
                }
                break;
              } else fprintf(stderr, "Received signal %u\n", siginfo.ssi_signo);
            } else msg = (char *) "connection read sigfd";
          }
        } else msg = (char *) "connection pthread_create";
      } else msg = (char *) "connection signalfd";
    } else msg = (char *) "connection pthread_sigmask";
  } else {
    fprintf(stderr, "Wrong arguments! The correct usage is: %s [port]. If no port is specified, it defaults to 1197\n", argv[0]);
    errno = EINVAL;
  }
  close(sigfd); sigfd = -1;
  // free(retval); // only free if thread worker function malloc()ed the return address
  perror(msg);
  msg = NULL;
  port = NULL;
  retval = NULL;
  exit(errno);
}
