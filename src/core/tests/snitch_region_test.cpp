/** @file tests/snitch_region_test.cpp
 *  The layout two processes have to agree on, pinned part by part.
 *
 *  The arithmetic is the easy half. The half that matters is that the parts do not
 *  overlap and that the sizes are derived rather than assumed, because the parent and
 *  the children read the same offsets and neither of them will notice a shift: they
 *  will both use their own idea of where a field is, and the disagreement shows up as a
 *  search that finds nothing rather than as an error.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_region.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::kRegionOrder;
    using ghostlock::backend::cve_2026_43499::leak::kRegionPartCount;
    using ghostlock::backend::cve_2026_43499::leak::layout_snitch_region;
    using ghostlock::backend::cve_2026_43499::leak::region_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::RegionLayout;
    using ghostlock::backend::cve_2026_43499::leak::RegionPart;
    using ghostlock::backend::cve_2026_43499::leak::RegionVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    RegionInputs typical() {
        RegionInputs inputs;
        inputs.header_bytes = 128U;
        inputs.collisions = 2U;
        inputs.thread_count = 4U;
        inputs.page_size = 4096U;
        inputs.word_bytes = 8U;
        inputs.handle_bytes = 8U;
        return inputs;
    }

    void the_order_is_the_interface() {
        expect(kRegionPartCount == 5U, "the region is five parts");
        expect(kRegionOrder[0] == RegionPart::Header, "the fixed fields come first");
        expect(kRegionOrder[1] == RegionPart::FutexAddresses, "then the addresses");
        expect(kRegionOrder[2] == RegionPart::Timestamps && kRegionOrder[3] == RegionPart::ThreadHandles,
               "then the timestamps and the thread handles");
        expect(kRegionOrder[4] == RegionPart::WaitPage, "and the wait page is last");
    }

    void the_sizes_follow_the_counts() {
        RegionLayout layout;
        expect(layout_snitch_region(typical(), layout) == RegionVerdict::Ok, "a full set of inputs lays out");
        expect(layout.offsets[0] == 0U && layout.bytes[0] == 128U, "the header starts at zero");
        expect(layout.bytes[1] == 3U * 8U,
               "the address array has one slot more than there are collisions");
        expect(layout.offsets[1] == 128U && layout.offsets[2] == 128U + 24U,
               "and each part starts where the last one ended");
        expect(layout.bytes[2] == 2U * 8U, "one timestamp per collision");
        expect(layout.bytes[3] == 4U * 8U, "one handle per thread");
        expect(layout.bytes[4] == 4096U, "and the wait page is a page");
        expect(layout.total_bytes == 128U + 24U + 16U + 32U + 4096U, "the total is the sum, once");
        for (std::size_t i = 1U; i < kRegionPartCount; ++i) {
            expect(layout.offsets[i] == layout.offsets[i - 1U] + layout.bytes[i - 1U],
                   "no part starts before the one before it ends");
        }
        expect(layout.offsets[kRegionPartCount - 1U] + layout.bytes[kRegionPartCount - 1U] == layout.total_bytes,
               "and the last part ends exactly at the end");
    }

    void what_cannot_be_laid_out() {
        RegionLayout layout;
        RegionInputs no_header = typical();
        no_header.header_bytes = 0U;
        expect(layout_snitch_region(no_header, layout) == RegionVerdict::GeometryMissing,
               "a header of no size is not a layout");
        RegionInputs no_collisions = typical();
        no_collisions.collisions = 0U;
        expect(layout_snitch_region(no_collisions, layout) == RegionVerdict::GeometryMissing,
               "nor a search with no collisions to record");
        RegionInputs no_page = typical();
        no_page.page_size = 0U;
        expect(layout_snitch_region(no_page, layout) == RegionVerdict::GeometryMissing,
               "nor a wait page of no size");
        RegionInputs widest = typical();
        widest.collisions = std::numeric_limits<std::size_t>::max();
        expect(layout_snitch_region(widest, layout) == RegionVerdict::GeometryMissing,
               "a collision count whose array would wrap is refused, not wrapped");
        RegionInputs huge_words = typical();
        huge_words.collisions = 3U;
        huge_words.word_bytes = std::numeric_limits<std::size_t>::max() / 2U;
        expect(layout_snitch_region(huge_words, layout) == RegionVerdict::GeometryMissing,
               "and so is an element size that would overflow its own array");
        RegionInputs overflowing_total = typical();
        overflowing_total.header_bytes = std::numeric_limits<std::size_t>::max() - 8U;
        expect(layout_snitch_region(overflowing_total, layout) == RegionVerdict::GeometryMissing,
               "as is a total that would run past the end of an address space");
        expect(layout.total_bytes == 0U, "and a refusal leaves no layout to map");
        expect(std::string_view(region_verdict_name(RegionVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "each refusal under the geometry name");
    }
}  // namespace

int main() {
    std::printf("snitch region: the order\n");
    the_order_is_the_interface();
    std::printf("snitch region: the sizes\n");
    the_sizes_follow_the_counts();
    std::printf("snitch region: what cannot be laid out\n");
    what_cannot_be_laid_out();
    if (g_failures != 0U) {
        std::printf("snitch region: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch region: PASS (five parts in order, sized from the counts)\n");
    return 0;
}
