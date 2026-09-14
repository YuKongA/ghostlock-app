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

  constexpr timespec start{.tv_sec = 10, .tv_nsec = 900000000};
  constexpr timespec end{.tv_sec = 12, .tv_nsec = 150000000};
  static_assert(elapsed_ms(start, end) == 1250.0);
  static_assert(elapsed_ms(end, start) == -1250.0);
  static_assert(reached(end, start));
  static_assert(!reached(start, end));

  assert(runtime_elapsed_between_ms(&start, &end) == 1250.0);
  return 0;
}
