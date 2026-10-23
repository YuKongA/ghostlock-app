#include "glk_log.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_file(void) {
  char path[] = "/tmp/glk_log_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  unlink(path);
  GlkFdSink sink;
  glk_fd_sink_init(&sink, fd);
  GlkLogSink log = {glk_fd_sink_write, &sink};
  glk_logf(&log, 0, "hello %d\n", 7);
  char buf[64] = {0};
  lseek(fd, 0, SEEK_SET);
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  assert(n == 8);
  assert(strcmp(buf, "hello 7\n") == 0);
  glk_fd_sink_close(&sink);
  assert(sink.fd == -1);
  glk_fd_sink_close(&sink);
}

static void test_truncate(void) {
  char path[] = "/tmp/glk_log_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  unlink(path);
  GlkFdSink sink;
  glk_fd_sink_init(&sink, fd);
  GlkLogSink log = {glk_fd_sink_write, &sink};
  char big[700];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  glk_logf(&log, 0, "%s", big);
  char buf[600] = {0};
  lseek(fd, 0, SEEK_SET);
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  assert(n > 0 && n < 512);
  assert(strcmp(buf + n - 3, "...") == 0);
  glk_fd_sink_close(&sink);
}

static void test_fifo_epipe(void) {
  int p[2];
  assert(pipe(p) == 0);
  close(p[0]);
  GlkFdSink sink;
  glk_fd_sink_init(&sink, p[1]);
  GlkLogSink log = {glk_fd_sink_write, &sink};
  signal(SIGPIPE, SIG_IGN);
  glk_logf(&log, 0, "closed");
  assert(atomic_load(&sink.disabled));
  glk_fd_sink_close(&sink);
}

int main(void) {
  test_file();
  test_truncate();
  test_fifo_epipe();
  return 0;
}
