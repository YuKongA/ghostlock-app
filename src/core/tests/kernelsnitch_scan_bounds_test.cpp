#include "../kernelsnitch/scan_bounds.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main(void) {
  assert(kernelsnitch_scan_limit(100, 20, 200) == 120);
  assert(kernelsnitch_scan_limit(100, 200, 150) == 150);
  assert(kernelsnitch_scan_limit(150, 20, 150) == 150);
  assert(kernelsnitch_scan_limit(SIZE_MAX - 8, 32, SIZE_MAX) == SIZE_MAX);
  puts("kernelsnitch_scan_bounds_test: ok");
  return 0;
}
