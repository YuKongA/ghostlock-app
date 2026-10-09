/** @file tests/scan_bounds_test.cpp
 *  One stride, clamped, saturating -- the retired tree own four cases and then some.
 *
 *  The four the retired test pinned are kept verbatim in meaning: a step that fits, a
 *  step longer than the room left, a start already at the end, and a start close
 *  enough to the largest address that adding the step would wrap. The additions are
 *  the boundaries around those: a step of zero, an end before the start, and the
 *  largest step there is.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "backend/cve_2026_43499/leak/scan_bounds.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::scan_limit;

    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();

    std::size_t g_failures = 0U;

    void expect(std::size_t got, std::size_t want, const char *what) {
        if (got != want) {
            std::printf("FAIL: %s (got %zu, wanted %zu)\n", what, got, want);
            ++g_failures;
        }
    }

    void the_retired_cases() {
        expect(scan_limit(100U, 20U, 200U), 120U, "a step that fits lands where it should");
        expect(scan_limit(100U, 200U, 150U), 150U, "a step longer than the room stops at the end");
        expect(scan_limit(150U, 20U, 150U), 150U, "a start already at the end stays there");
        expect(scan_limit(kMax - 8U, 32U, kMax), kMax, "and a step that would wrap stops at the top");
    }

    void the_edges_around_them() {
        expect(scan_limit(100U, 0U, 200U), 100U, "a step of nothing goes nowhere");
        expect(scan_limit(300U, 20U, 200U), 200U,
               "an end behind the start still clamps, rather than running on");
        expect(scan_limit(0U, kMax, kMax), kMax, "the largest step reaches the top, not past it");
        expect(scan_limit(kMax, 1U, kMax), kMax, "and the top plus one is still the top");
    }
}  // namespace

int main() {
    std::printf("scan bounds: the retired cases\n");
    the_retired_cases();
    std::printf("scan bounds: the edges around them\n");
    the_edges_around_them();
    if (g_failures != 0U) {
        std::printf("scan bounds: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("scan bounds: PASS (one stride, clamped, saturating)\n");
    return 0;
}
