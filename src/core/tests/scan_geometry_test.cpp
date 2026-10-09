/** @file tests/scan_geometry_test.cpp
 *  The leak scan's shape: what the range becomes, and where the walk must stop.
 *
 *  The case worth reading twice is the end of the range: the retired tree rounded it
 *  up only when it was not already aligned, so an aligned end stays where it is
 *  instead of gaining a stride. That is one coarse stride of scanning on every range
 *  that happens to land on a boundary, and it is the kind of difference nobody
 *  notices until a run takes longer than it used to.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/scan_geometry.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::align_scan_range;
    using ghostlock::backend::cve_2026_43499::leak::ScanGeometryVerdict;
    using ghostlock::backend::cve_2026_43499::leak::scan_ceiling;
    using ghostlock::backend::cve_2026_43499::leak::scan_geometry_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::strides_are_usable;

    constexpr std::uint64_t kCoarse = 1ULL << 30;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_range_is_aligned_the_way_the_tree_did_it() {
        ScanRange out;
        ScanRange ragged;
        ragged.start = kCoarse + 4096U;
        ragged.end = 4U * kCoarse + 4096U;
        expect(align_scan_range(ragged, kCoarse, out) == ScanGeometryVerdict::Ok,
               "a ragged range can be aligned");
        expect(out.start == kCoarse, "the start rounds down to the coarse boundary");
        expect(out.end == 5U * kCoarse, "and the end rounds up to one");
        ScanRange tidy;
        tidy.start = 2U * kCoarse;
        tidy.end = 4U * kCoarse;
        expect(align_scan_range(tidy, kCoarse, out) == ScanGeometryVerdict::Ok, "ok");
        expect(out.start == 2U * kCoarse && out.end == 4U * kCoarse,
               "an already aligned range keeps both ends, gaining no stride");
        ScanRange top;
        top.start = 0U;
        top.end = UINT64_MAX;
        expect(align_scan_range(top, kCoarse, out) == ScanGeometryVerdict::GeometryMissing,
               "a rounding that would wrap is refused rather than shrinking the end");
        ScanRange bad_coarse;
        bad_coarse.start = 0U;
        bad_coarse.end = 1024U;
        expect(align_scan_range(bad_coarse, 3000U, out) == ScanGeometryVerdict::GeometryMissing,
               "and a coarse stride that is not a power of two cannot align anything");
        expect(align_scan_range(bad_coarse, 0U, out) == ScanGeometryVerdict::GeometryMissing,
               "nor can a stride of nothing");
        expect(std::string_view(scan_geometry_verdict_name(ScanGeometryVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "refused under the geometry name");
    }

    void the_walk_stops_at_the_lower_end() {
        std::uint64_t ceiling = 0U;
        expect(scan_ceiling(0xffff800000000000ULL, 0xffffc88000000000ULL, ceiling) ==
                   ScanGeometryVerdict::Ok,
               "two ends make a ceiling");
        expect(ceiling == 0xffff800000000000ULL, "and it is the lower of them");
        expect(scan_ceiling(0xffffc88000000000ULL, 0xffffc88000000000ULL, ceiling) ==
                   ScanGeometryVerdict::Ok,
               "equal ends are fine");
        expect(ceiling == 0xffffc88000000000ULL, "and give that value");
        expect(scan_ceiling(0U, 0xffffc88000000000ULL, ceiling) == ScanGeometryVerdict::GeometryMissing,
               "a direct map that was never measured refuses the scan");
        expect(scan_ceiling(0xffff800000000000ULL, 0U, ceiling) == ScanGeometryVerdict::GeometryMissing,
               "and so does a kernel build with no identity end");
        ScanStrides strides;
        strides.coarse = kCoarse;
        strides.slab = 1U << 17U;
        strides.candidate = 512U;
        expect(strides_are_usable(strides), "three strides of some size are usable");
        strides.candidate = 0U;
        expect(!strides_are_usable(strides), "a walk that steps by zero never advances");
    }
}  // namespace

int main() {
    std::printf("scan geometry: the range\n");
    the_range_is_aligned_the_way_the_tree_did_it();
    std::printf("scan geometry: the ceiling\n");
    the_walk_stops_at_the_lower_end();
    if (g_failures != 0U) {
        std::printf("scan geometry: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("scan geometry: PASS (range aligned, walk bounded)\n");
    return 0;
}
