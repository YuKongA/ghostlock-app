/** @file tests/route_status_test.cpp
 *  What a route reports, and the two questions a caller actually asks of it.
 *
 *  The layout is checked where it is declared, so a field moved by accident stops the
 *  build. What is left for a test is the pair of predicates: a status is clean only when
 *  both sides were put back, and dirty is wider than the dirty code -- a failure that left
 *  the kernel armed is dirty whatever it called itself.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/route/route_status.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::RouteResultCode;
    using ghostlock::backend::cve_2026_43499::route::RouteStatus;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void clean_means_both_sides() {
        RouteStatus ok;
        ok.code = RouteResultCode::Ok;
        ok.userspace_clean = 1;
        ok.kernel_disarmed = 1;
        expect(ok.is_clean() && !ok.is_dirty(), "both sides put back is clean");
        RouteStatus half;
        half.code = RouteResultCode::Ok;
        half.userspace_clean = 1;
        expect(!half.is_clean() && half.is_dirty(),
               "a kernel left armed is dirty even when the code says ok");
        RouteStatus other;
        other.code = RouteResultCode::Ok;
        other.kernel_disarmed = 1;
        expect(!other.is_clean() && other.is_dirty(), "and so is userspace left behind");
    }

    void a_dirty_code_is_dirty_by_name() {
        RouteStatus dirty;
        dirty.code = RouteResultCode::DirtyFailure;
        dirty.userspace_clean = 1;
        dirty.kernel_disarmed = 1;
        expect(dirty.is_dirty(), "the dirty code is dirty on its own word");
        RouteStatus failed;
        failed.code = RouteResultCode::DirtyFailure;
        expect(failed.is_dirty() && !failed.is_clean(), "and cleaning both sides needs both flags");
        RouteStatus retryable;
        retryable.code = RouteResultCode::Retryable;
        retryable.userspace_clean = 1;
        retryable.kernel_disarmed = 1;
        expect(!retryable.is_dirty(), "a clean retryable failure is not dirty, only retryable");
        RouteStatus unsupported;
        unsupported.code = RouteResultCode::Unsupported;
        unsupported.userspace_clean = 1;
        unsupported.kernel_disarmed = 1;
        expect(!unsupported.is_dirty(), "and so is a route that never ran");
        expect(static_cast<std::int32_t>(RouteResultCode::Ok) == 0,
               "the codes keep the retired tree's numbering");
        expect(static_cast<std::int32_t>(RouteResultCode::Unsupported) == 4,
               "through to the last of them");
    }
}  // namespace

int main() {
    std::printf("route status: clean is both sides\n");
    clean_means_both_sides();
    std::printf("route status: dirty is wider than the code\n");
    a_dirty_code_is_dirty_by_name();
    if (g_failures != 0U) {
        std::printf("route status: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("route status: PASS (clean needs both sides, dirty is wider than the code)\n");
    return 0;
}
