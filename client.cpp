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
  int sfd = -1, len = -1, nsent = -1;
  const char *port = NULL, *path = NULL, *msg = (char *) "Client execution terminated";
  errno = EXIT_SUCCESS;
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
      sfd = client_sfd(argv[1], port);
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
              fprintf(stderr, "\nsend: %s\n", strerror_r(errno, buf, GBUF_SIZE));
              break;
            }
          } else { printf(" success!\n"); errno = EXIT_SUCCESS; }
        }
      } else msg = (char *) "Could not initialize socket";
      fclose(f); f = NULL;
    } else msg = (char *) "Could not open file";
  } else {
    fprintf(stderr, "Wrong arguments! The correct usage is: %s host [port] file. If no port is specified, it defaults to 1197\n", argv[0]);
    errno = EINVAL;
  }
  if (sfd != -1) {
    close(sfd);
    sfd = -1;
  }
  perror(msg);
  port = NULL;
  path = NULL;
  msg = NULL;
  exit(errno);
}
