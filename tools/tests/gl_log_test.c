#include "general/gl_log.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  char path[] = "/tmp/gl_log_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  unlink(path);
  gl_log_set_timestamps(0);
  gl_log_set_min_level(GL_LOG_DEBUG);
  int owned = gl_log_attach_fd(fd, 0, GL_LOG_DEBUG);
  assert(owned >= 0);
  assert(owned != fd);
  assert(gl_log_export_fd() == owned);
  assert(fcntl(owned, F_GETFD) >= 0);
  close(fd);
  gl_log_write(GL_LOG_INFO, "gl_log_test.c", 9, "hello %d", 7);
#if defined(__ANDROID__)
  char buf[256] = {0};
  lseek(owned, 0, SEEK_SET);
  ssize_t n = read(owned, buf, sizeof(buf) - 1);
  assert(n > 0);
  assert(strstr(buf, "[*] hello 7") != NULL);
#endif
  assert(gl_log_attach_fd(-1, 0, GL_LOG_INFO) == -1);
  assert(gl_log_export_fd() == -1);
  assert(gl_log_level_from_name("warn") == GL_LOG_WARN);
  assert(gl_log_level_from_name("unknown") == GL_LOG_INFO);
  return 0;
}
