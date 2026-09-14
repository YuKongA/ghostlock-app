#include "routes/route_status.h"

#include <cassert>
#include <cstddef>

int main() {
  static_assert(static_cast<int>(ROUTE_OK) == 0);
  static_assert(static_cast<int>(ROUTE_RETRYABLE) == 1);
  static_assert(static_cast<int>(ROUTE_FALLBACK_SAFE) == 2);
  static_assert(static_cast<int>(ROUTE_DIRTY_FAILURE) == 3);
  static_assert(static_cast<int>(ROUTE_UNSUPPORTED) == 4);
  static_assert(sizeof(RouteStatus) == sizeof(int) * 5);
  static_assert(offsetof(RouteStatus, kernel_disarmed) == sizeof(int) * 4);

  constexpr RouteStatus fallback{
      .code = ROUTE_FALLBACK_SAFE,
      .userspace_clean = 1,
      .kernel_disarmed = 1,
  };
  static_assert(fallback.is_clean());
  static_assert(!fallback.is_dirty());
  static_assert(fallback.can_fallback());

  constexpr RouteStatus dirty{
      .code = ROUTE_DIRTY_FAILURE,
      .userspace_clean = 0,
      .kernel_disarmed = 1,
  };
  static_assert(!dirty.is_clean());
  static_assert(dirty.is_dirty());
  static_assert(!dirty.can_fallback());
  return 0;
}
