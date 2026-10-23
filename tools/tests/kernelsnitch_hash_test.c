#define _GNU_SOURCE
#include "futex_hash.h"
#include "scan_bounds.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t bucket(size_t addr, size_t mm, uint32_t size) {
  return futex_hash(addr, mm, size);
}

int main(void) {
  assert(futex_hash_table_size_for(8) == 2048);
  assert(scan_limit(0, 10, 100) == 10);
  assert(scan_limit(95, 10, 100) == 100);
  assert(scan_limit(100, 10, 100) == 100);
  assert(scan_limit((size_t)-1, 10, (size_t)-1) == (size_t)-1);

  const size_t mm = 0xffff888000000000ULL;
  int found = 0;
  for (size_t a = 0x100000000ULL; a < 0x100200000ULL; a += 0x1000) {
    uint32_t h2 = bucket(a, mm, 2048);
    uint32_t h4 = bucket(a, mm, 4096);
    for (size_t b = a + 0x1000; b < 0x100200000ULL; b += 0x1000) {
      if (bucket(b, mm, 2048) == h2 && bucket(b, mm, 4096) != h4) {
        printf("pair 2048=%u 4096=%u/%u\n", h2, h4, bucket(b, mm, 4096));
        found = 1;
        break;
      }
    }
    if (found) break;
  }
  assert(found);
  return 0;
}
