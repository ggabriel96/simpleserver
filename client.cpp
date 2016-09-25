#include "gsock.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

int main(int argc, const char *argv[]) {
  FILE *f = NULL;
  char buf[GBUF_SIZE];
  int exit_status = EXIT_SUCCESS, sfd = -1, len = -1, nsent = -1;
  if (argc == 4) {
    f = fopen(argv[3], "r");
    if (f != NULL) {
      nsent = 0;
      sfd = client_sfd(argv[1], argv[2]);
      if (sfd != -1) {
        while (fgets(buf, GBUF_SIZE, f) != NULL) {
          len = strlen(buf); buf[--len] = '\0';
          printf("Attempting to send '%s' (%d byte%s)...", buf, len, len == 1 ? "" : "s");
          nsent = send(sfd, buf, len, 0);
          if (nsent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              printf("\nsend: %s. Trying again...\n", strerror_r(errno, buf, GBUF_SIZE));
              fseek(f, -len - 1, SEEK_CUR);
            } else {
              fprintf(stderr, "\nsend error: %s\n", strerror_r(errno, buf, GBUF_SIZE));
              exit_status = errno;
              break;
            }
          }
          else printf(" success!\n");
        }
      } else {
        exit_status = errno;
        perror("Could not initialize socket");
      }
    } else {
      exit_status = errno;
      perror("Could not open file");
    }
  } else {
    fprintf(stderr, "# Wrong parameters\n! Correct usage: %s <host> <port> <file>", argv[0]);
    exit_status = EXIT_FAILURE;
  }
  if (sfd != -1) { close(sfd); sfd = -1; }
  exit(exit_status);
}
