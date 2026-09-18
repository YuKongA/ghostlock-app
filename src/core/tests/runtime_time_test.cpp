#include "runtime_time.h"

#include <cassert>

int main() {
  using ghostlock::runtime_time::elapsed_ms;
  using ghostlock::runtime_time::normalize;
  using ghostlock::runtime_time::reached;

  constexpr timespec unnormalized{.tv_sec = 3, .tv_nsec = -250000000};
  constexpr timespec normalized = normalize(unnormalized);
  static_assert(normalized.tv_sec == 2);
  static_assert(normalized.tv_nsec == 750000000);

  constexpr timespec overflow{.tv_sec = 1, .tv_nsec = 1500000000};
  constexpr timespec overflow_normalized = normalize(overflow);
  static_assert(overflow_normalized.tv_sec == 2);
  static_assert(overflow_normalized.tv_nsec == 500000000);

  constexpr timespec start{.tv_sec = 10, .tv_nsec = 900000000};
  constexpr timespec end{.tv_sec = 12, .tv_nsec = 150000000};
  static_assert(elapsed_ms(start, end) == 1250.0);
  static_assert(elapsed_ms(end, start) == -1250.0);
  static_assert(elapsed_ms(start, start) == 0.0);
  static_assert(reached(end, start));
  static_assert(!reached(start, end));
  static_assert(reached(start, start));

  assert(runtime_elapsed_between_ms(&start, &end) == 1250.0);

  const timespec first = ghostlock::runtime_time::monotonic_now();
  const timespec second = ghostlock::runtime_time::monotonic_now();
  assert(ghostlock::runtime_time::reached(second, first));
  assert(runtime_elapsed_ms(&first) >= 0.0);
  return 0;
}
