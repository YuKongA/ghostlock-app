#include "route/route_status.h"

#include <cassert>
#include <cstddef>

using namespace ghostlock;

int32_t main() {
    static_assert(static_cast<int32_t>(ghostlock::route::ROUTE_OK) == 0);
    static_assert(static_cast<int32_t>(ghostlock::route::ROUTE_RETRYABLE) == 1);
    static_assert(static_cast<int32_t>(ghostlock::route::ROUTE_FALLBACK_SAFE) == 2);
    static_assert(static_cast<int32_t>(ghostlock::route::ROUTE_DIRTY_FAILURE) == 3);
    static_assert(static_cast<int32_t>(ghostlock::route::ROUTE_UNSUPPORTED) == 4);
    static_assert(sizeof(ghostlock::route::RouteStatus) == sizeof(int32_t) * 5);
    static_assert(offsetof(ghostlock::route::RouteStatus, kernel_disarmed) == sizeof(int32_t) * 4);

    constexpr ghostlock::route::RouteStatus fallback{
        .code = ghostlock::route::ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    };
    static_assert(fallback.is_clean());
    static_assert(!fallback.is_dirty());
    static_assert(fallback.can_fallback());

    constexpr ghostlock::route::RouteStatus dirty{
        .code = ghostlock::route::ROUTE_DIRTY_FAILURE,
        .userspace_clean = 0,
        .kernel_disarmed = 1,
    };
    static_assert(!dirty.is_clean());
    static_assert(dirty.is_dirty());
    static_assert(!dirty.can_fallback());
    return 0;
}
