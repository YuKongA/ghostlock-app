#ifndef GHOSTLOCK_RUNTIME_TIME_H
#define GHOSTLOCK_RUNTIME_TIME_H

#include <time.h>

#ifdef __cplusplus

#include <chrono>

namespace ghostlock::runtime_time {

using Nanoseconds = std::chrono::nanoseconds;
using Milliseconds = std::chrono::duration<double, std::milli>;

[[nodiscard]] constexpr Nanoseconds to_duration(timespec value) noexcept {
  return std::chrono::seconds(value.tv_sec) +
         std::chrono::nanoseconds(value.tv_nsec);
}

[[nodiscard]] constexpr timespec normalize(timespec value) noexcept {
  constexpr long kNanosPerSecond = 1000000000L;
  value.tv_sec += value.tv_nsec / kNanosPerSecond;
  value.tv_nsec %= kNanosPerSecond;
  if (value.tv_nsec < 0) {
    --value.tv_sec;
    value.tv_nsec += kNanosPerSecond;
  }
  return value;
}

[[nodiscard]] constexpr double elapsed_ms(timespec start,
                                          timespec end) noexcept {
  return Milliseconds(to_duration(end) - to_duration(start)).count();
}

[[nodiscard]] inline timespec monotonic_now() noexcept {
  timespec now{};
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return now;
}

[[nodiscard]] constexpr bool reached(timespec now,
                                     timespec deadline) noexcept {
  return to_duration(now) >= to_duration(deadline);
}

}  // namespace ghostlock::runtime_time

static inline double runtime_elapsed_between_ms(const timespec *start,
                                                const timespec *end) {
  return ghostlock::runtime_time::elapsed_ms(*start, *end);
}

static inline double runtime_elapsed_ms(const timespec *reference) {
  const timespec now = ghostlock::runtime_time::monotonic_now();
  return runtime_elapsed_between_ms(reference, &now);
}

#else

static inline double runtime_elapsed_ms(const struct timespec *reference) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - reference->tv_sec) * 1000.0 +
         (now.tv_nsec - reference->tv_nsec) / 1e6;
}

#endif

#endif
