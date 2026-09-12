#ifndef GHOSTLOCK_RUNTIME_TIME_H
#define GHOSTLOCK_RUNTIME_TIME_H

#include <time.h>

static inline double runtime_elapsed_ms(const struct timespec *reference) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - reference->tv_sec) * 1000.0 +
         (now.tv_nsec - reference->tv_nsec) / 1e6;
}

#endif
