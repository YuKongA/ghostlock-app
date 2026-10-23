#define _GNU_SOURCE
#include "glk_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void glk_fd_sink_init(GlkFdSink *sink, int fd) {
  if (!sink) return;
  sink->fd = fd;
  sink->kind = GLK_FD_FILE;
  atomic_init(&sink->disabled, fd < 0);
  atomic_init(&sink->in_pi, false);
  if (fd < 0) return;

  struct stat st;
  if (fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode)) {
    sink->kind = GLK_FD_FIFO;
    return;
  }
  int type = 0;
  socklen_t len = sizeof(type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
    sink->kind = GLK_FD_SOCKET;
  }
}

void glk_fd_sink_close(GlkFdSink *sink) {
  if (!sink) return;
  atomic_store(&sink->disabled, true);
  int fd = sink->fd;
  sink->fd = -1;
  if (fd >= 0) close(fd);
}

void glk_fd_sink_write(void *user, int level, const char *message) {
  (void)level;
  GlkFdSink *sink = (GlkFdSink *)user;
  if (!sink || !message) return;
  if (atomic_load(&sink->disabled) || atomic_load(&sink->in_pi)) return;
  int fd = sink->fd;
  if (fd < 0) {
    atomic_store(&sink->disabled, true);
    return;
  }

  ssize_t n;
  if (sink->kind == GLK_FD_SOCKET) {
    n = send(fd, message, strlen(message), MSG_NOSIGNAL);
  } else if (sink->kind == GLK_FD_FIFO) {
#if defined(__linux__)
    sigset_t set;
    sigset_t old;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, &old);
    n = write(fd, message, strlen(message));
    int saved = errno;
    if (n < 0 && saved == EPIPE) {
      struct timespec zero = {0, 0};
      while (sigtimedwait(&set, NULL, &zero) < 0 && errno == EINTR) {
      }
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    errno = saved;
#else
    n = write(fd, message, strlen(message));
#endif
  } else {
    n = write(fd, message, strlen(message));
  }
  if (n < 0) atomic_store(&sink->disabled, true);
}

void glk_logf(GlkLogSink *sink, int level, const char *fmt, ...) {
  if (!sink || !sink->write || !fmt) return;
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if ((size_t)n >= sizeof(buf)) {
    memcpy(buf + sizeof(buf) - 4, "...", 3);
    buf[sizeof(buf) - 1] = '\0';
  }
  sink->write(sink->user, level, buf);
}

void glk_log_pi_enter(GlkLogSink *sink) {
  if (sink && sink->write == glk_fd_sink_write && sink->user) {
    atomic_store(&((GlkFdSink *)sink->user)->in_pi, true);
  }
}

void glk_log_pi_exit(GlkLogSink *sink) {
  if (sink && sink->write == glk_fd_sink_write && sink->user) {
    atomic_store(&((GlkFdSink *)sink->user)->in_pi, false);
  }
}
