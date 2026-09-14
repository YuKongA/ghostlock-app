#include "support/cpp_link_probe.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void) {
  errno = EAGAIN;
  assert(ghostlock_cpp_link_probe("cpp", errno) == 6 + EAGAIN);
  assert(errno == EAGAIN);
  puts("cpp_link_probe_test: ok");
  return 0;
}
