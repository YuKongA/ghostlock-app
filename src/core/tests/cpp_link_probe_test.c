#include "tests/cpp_link_probe.h"

#include <cassert>
#include <cerrno>
#include <cstdio>

int main(void) {
  errno = EAGAIN;
  assert(ghostlock_cpp_link_probe("cpp", errno) == 6 + EAGAIN);
  assert(errno == EAGAIN);
  puts("cpp_link_probe_test: ok");
  return 0;
}
