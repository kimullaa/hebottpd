#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include "service.h"
#include "main.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAX_BACKLOG 100

typedef void (*sighandler_t)(int);


static void install_signal_handlers(void);
static void trap_signal(int sig, sighandler_t handler);
static void signal_exit(int sig);
static void install_signal_handlers(void);
static int listen_socket(char *port);

static void trap_signal(int sig, sighandler_t handler) {
  struct sigaction act;

  act.sa_handler = handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESTART;
  if (sigaction(sig, &act, NULL) < 0) {
    log_exit("sigaction() failed : %s", strerror(errno));
  }
}

void* xalloc(size_t sz) {
  void *p;
  if ((p = malloc(sz)) == NULL) {
    log_exit("failed to allocate memory");
  }
  return p;
}

void log_exit(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  va_end(args);

  exit(1);
}

static void signal_exit(int sig) {
  log_exit("exit by signal %d", sig);
}

static void install_signal_handlers(void) {
  trap_signal(SIGPIPE, &signal_exit);
}

static int listen_socket(char *port) {
  struct addrinfo hints, *res, *ai;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, port, &hints, &res) != 0) {
    log_exit("getaddrinfo");
  }

  for (ai = res; ai; ai = ai->ai_next) {
    int sock;

    sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock < 0) continue;
    if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
      close(sock);
      continue;
    }

    if (listen(sock, MAX_BACKLOG) < 0) {
      close(sock);
      continue;
    }

    freeaddrinfo(res);
    return sock;
  }

  log_exit("failed to listen socket");
  return -1;
}

static void server_main(int fd, char *docroot) {

  while(true) {
    int sock;
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof addr;
    if ((sock = accept(fd, (struct sockaddr*) &addr, &addrlen)) < 0) {
      log_exit("accept(2) failed");
    }

    pid_t pid;
    if ((pid = fork()) == -1) {
      log_exit("can't fork");
    }

    // child
    if (pid == 0) {
      FILE *in, *out;
      if ((in = fdopen(sock, "r")) == NULL) {
	log_exit("cant open ");
      }
      if ((out = fdopen(sock, "w")) == NULL) {
	log_exit("cant open ");
      }
      service(in, out, docroot);
      exit(0);
    }

    close(sock);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    log_exit("Usage: %s <docroot>\n", argv[0]);
  }
  install_signal_handlers();

  int fd;
  fd = listen_socket("18080");

  server_main(fd, argv[1]);
  exit(0);
}
