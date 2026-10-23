#ifndef KERNELSNITCH_SCAN_BOUNDS_H
#define KERNELSNITCH_SCAN_BOUNDS_H

#include <stddef.h>

/* Return min(start + span, end) without size_t overflow. */
static inline size_t scan_limit(size_t start, size_t span, size_t end) {
  if (start >= end || span >= end - start) return end;
  return start + span;
}

#endif
