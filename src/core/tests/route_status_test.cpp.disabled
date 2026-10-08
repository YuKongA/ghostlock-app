#include "backend/cve_2026_43499/route/route_status.h"

#include <cassert>
#include <cstddef>

using namespace ghostlock;

int32_t main() {
    static_assert(static_cast<int32_t>(ghostlock::backend::cve_2026_43499::route::ROUTE_OK) == 0);
    static_assert(static_cast<int32_t>(ghostlock::backend::cve_2026_43499::route::ROUTE_RETRYABLE) == 1);
    static_assert(static_cast<int32_t>(ghostlock::backend::cve_2026_43499::route::ROUTE_FALLBACK_SAFE) == 2);
    static_assert(static_cast<int32_t>(ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE) == 3);
    static_assert(static_cast<int32_t>(ghostlock::backend::cve_2026_43499::route::ROUTE_UNSUPPORTED) == 4);
    static_assert(sizeof(ghostlock::backend::cve_2026_43499::route::RouteStatus) == sizeof(int32_t) * 5);
    static_assert(offsetof(ghostlock::backend::cve_2026_43499::route::RouteStatus, kernel_disarmed) == sizeof(int32_t) * 4);

    /* R6a: FallbackSafe is now just a clean-failure code; there is no
     * can_fallback() capability any more (fallback left the wire). */
    constexpr ghostlock::backend::cve_2026_43499::route::RouteStatus clean{
        .code = ghostlock::backend::cve_2026_43499::route::ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    };
    static_assert(clean.is_clean());
    static_assert(!clean.is_dirty());

    constexpr ghostlock::backend::cve_2026_43499::route::RouteStatus dirty{
        .code = ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE,
        .userspace_clean = 0,
        .kernel_disarmed = 1,
    };
    static_assert(!dirty.is_clean());
    static_assert(dirty.is_dirty());
    return 0;
}
