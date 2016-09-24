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
  pthread_attr_t thread_attr;
  struct signalfd_siginfo fdsi;
  int sigfd = -1, ssfd = -1, nt = -1, *retval = NULL;
  const char *port = NULL, *msg = (char *) "Server execution terminated";
  errno = EXIT_SUCCESS;
  if (argc <= 2) {
    if (argc == 1) port = (char *) "1197";
    else port = argv[1];
    // errno = pthread_attr_init(&thread_attr);
    // if (!errno) {
    //   errno = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
    //   if (!errno) {
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
          printf("Starting server on port %s and %d threads\n", port, NT);
          ssfd = server_sfd(port);
          if (ssfd != -1) {
            sigfd = signalfd(-1, &sigmask, 0);
            if (sigfd != -1) {
              for (nt = 0; nt < NT; nt++) {
                // for detached thread:
                // if ((errno = pthread_create(&thread_id, &thread_attr, server, (void *) &ssfd)) == 0)
                if ((errno = pthread_create(&thread_id, NULL, server, (void *) &ssfd)) == 0)
                  threads.push_back(thread_id);
              }
              if (!errno) {
                nread = read(sigfd, &fdsi, sizeof (struct signalfd_siginfo));
                if (nread == sizeof (struct signalfd_siginfo)) {
                  if (fdsi.ssi_signo == SIGHUP
                      || fdsi.ssi_signo == SIGINT
                      || fdsi.ssi_signo == SIGQUIT
                      || fdsi.ssi_signo == SIGABRT
                      || fdsi.ssi_signo == SIGTERM
                      || fdsi.ssi_signo == SIGSTOP
                      || fdsi.ssi_signo == SIGTSTP) {
                        sigv.sival_int = fdsi.ssi_signo;
                    for (auto& th: threads) {
                      if ((errno = pthread_sigqueue(th, SIGUSR1, sigv)) == 0) {
                        printf("\nCancel request queued for thread %lu...\n", th);
                        if ((errno = pthread_join(th, (void **) &retval)) == 0) {
                          perror(NULL);
                        } else perror("pthread_join");
                      } else perror("pthread_sigqueue");
                    }
                  } else fprintf(stderr, "Received signal %u\n", fdsi.ssi_signo);
                } else msg = (char *) "sigfd recv";
              } else msg = (char *) "Could not create all threads";
            } else msg = (char *) "signalfd";
          } else msg = (char *) "Could not initialize socket";
        } else msg = (char *) "pthread_sigmask";
      // } else msg = (char *) "pthread_attr_setdetachstate";
    // } else msg = (char *) "pthread_attr_init";
  } else {
    fprintf(stderr, "Wrong arguments! The correct usage is: %s [port]. If no port is specified, it defaults to 1197\n", argv[0]);
    errno = EINVAL;
  }
  if (ssfd != -1) { close(ssfd); ssfd = -1; }
  // free(retval); // do not free if returning &errno
  retval = NULL;
  perror(msg);
  exit(errno);
}
