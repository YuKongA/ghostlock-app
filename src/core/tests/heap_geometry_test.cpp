/** @file tests/heap_geometry_test.cpp
 *  The region a device needs, and the check that it is the one it has.
 *
 *  Two directions are tested. A device whose page size is what the sizes were built
 *  for passes; a device twice that size, checked against a region built for the
 *  smaller page, is refused -- which is the retired tree's failure mode made
 *  visible: it assumed one page size and would have worked on the wrong region
 *  without saying anything.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/memory/heap_geometry.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::check_region_matches_device;
    using ghostlock::backend::cve_2026_43499::memory::heap_geometry_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::HeapGeometryVerdict;
    using ghostlock::backend::cve_2026_43499::memory::kMmOrder;
    using ghostlock::backend::cve_2026_43499::memory::kSkbSendFactor;
    using ghostlock::backend::cve_2026_43499::memory::region_for;
    using ghostlock::backend::cve_2026_43499::memory::send_size_for;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_region_follows_the_page() {
        expect(kMmOrder == 3U, "the mechanism prepares an order-three region");
        expect(region_for(4096U) == 32768U, "four kilobyte pages make a thirty-two kilobyte region");
        expect(region_for(16384U) == 131072U,
               "and sixteen kilobyte pages make one four times that, not the same one");
        expect(region_for(0U) == 0U, "a page of no size has no region");
        expect(kSkbSendFactor == 2U, "the send carries two regions");
        expect(send_size_for(32768U) == 65536U, "which is what that region sends");
    }

    void a_region_that_is_not_this_device() {
        /* The control: the sizes were built for this page size. */
        expect(check_region_matches_device(region_for(4096U), 4096U) == HeapGeometryVerdict::Ok,
               "a region built for four kilobyte pages passes on such a device");
        expect(std::string_view(heap_geometry_verdict_name(HeapGeometryVerdict::Ok)) ==
                   std::string_view("ok"),
               "and is just ok");
        /* The case the retired tree could not have noticed: a device with twice the
         *  page size, checked against a region derived from the smaller one. */
        const std::uint64_t built_for_4k = region_for(4096U);
        expect(check_region_matches_device(built_for_4k, 8192U) == HeapGeometryVerdict::GeometryMissing,
               "the same region on a device with larger pages is refused");
        expect(std::string_view(heap_geometry_verdict_name(HeapGeometryVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "under the geometry refusal, not the parameter one");
        expect(check_region_matches_device(0U, 4096U) == HeapGeometryVerdict::GeometryMissing,
               "and so is a device that reports no page size at all");
        expect(check_region_matches_device(region_for(16384U), 4096U) == HeapGeometryVerdict::GeometryMissing,
               "as is a region built for pages this device does not have");
    }
}  // namespace

int main() {
    std::printf("heap geometry: the region\n");
    the_region_follows_the_page();
    std::printf("heap geometry: a region that is not this device\n");
    a_region_that_is_not_this_device();
    if (g_failures != 0U) {
        std::printf("heap geometry: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("heap geometry: PASS (the region follows the page, and a mismatch is refused)\n");
    return 0;
}
