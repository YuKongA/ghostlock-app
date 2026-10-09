/** @file tests/page_geometry_test.cpp
 *  The two decisions a page preparation makes about the memory it is in.
 *
 *  The alignment is the retired tree's mask, and the cases are chosen to show what
 *  it does at the boundaries rather than to pin one page size: several power-of-two
 *  sizes are tried, an address already aligned stays put, an address one byte above
 *  the base rounds down, and a size that is not a power of two is refused rather
 *  than turned into an address that belongs to no page.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/memory/page_geometry.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::align_base;
    using ghostlock::backend::cve_2026_43499::memory::geometry_is_usable;
    using ghostlock::backend::cve_2026_43499::memory::geometry_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::GeometryVerdict;
    using ghostlock::backend::cve_2026_43499::memory::PageGeometry;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_alignment_rounds_down() {
        for (const std::uint64_t page : {4096ULL, 8192ULL, 32768ULL, 1ULL << 20}) {
            /* A base that is aligned for THIS page size: taking one address and
             *  assuming it is aligned for every size is the mistake this case exists
             *  to catch, since a larger page can start inside the address. */
            const std::uint64_t base = align_base(0x1f0000ULL, page);
            expect(base != 0U, "the region has a base");
            expect(align_base(base, page) == base, "an aligned address stays put");
            expect(align_base(base + 1U, page) == base,
                   "an address one byte above the base rounds down to it");
            expect(align_base(base + page - 1U, page) == base,
                   "and so does the last byte of the region");
            expect(align_base(base + page, page) == base + page,
                   "while the first byte of the next region is already aligned");
            expect(align_base(0U, page) == 0U, "nothing stays nothing");
        }
        expect(align_base(0x1234ULL, 0U) == 0U, "a page of no size aligns nothing");
        expect(align_base(0x1234ULL, 3000U) == 0U,
               "a size that is not a power of two is refused, not masked");
        expect(align_base(0x1234ULL, 1ULL << 63) == 0U,
               "including one whose mask would keep the wrong bits");
    }

    void the_geometry_is_checked() {
        PageGeometry good;
        good.align_region_size = 4096U;
        good.send_size = 1024U;
        good.socket_buffer = 1024 * 1024;
        expect(geometry_is_usable(good), "a geometry with all three set is usable");
        PageGeometry no_page = good;
        no_page.align_region_size = 0U;
        expect(!geometry_is_usable(no_page), "no page size is not a geometry");
        PageGeometry ragged = good;
        ragged.align_region_size = 4097U;
        expect(!geometry_is_usable(ragged), "nor is a page size that is not a power of two");
        PageGeometry no_send = good;
        no_send.send_size = 0U;
        expect(!geometry_is_usable(no_send), "nor a send of nothing");
        PageGeometry no_buffer = good;
        no_buffer.socket_buffer = 0;
        expect(!geometry_is_usable(no_buffer), "nor a socket buffer of nothing");
        expect(std::string_view(geometry_verdict_name(GeometryVerdict::GeometryInvalid)) ==
                   std::string_view("param-invalid"),
               "and the refusal has the tree's name for a bad parameter");
        expect(std::string_view(geometry_verdict_name(GeometryVerdict::Ok)) ==
                   std::string_view("ok"),
               "while a usable one is just ok");
    }
}  // namespace

int main() {
    std::printf("page geometry: the alignment\n");
    the_alignment_rounds_down();
    std::printf("page geometry: the checks\n");
    the_geometry_is_checked();
    if (g_failures != 0U) {
        std::printf("page geometry: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page geometry: PASS (alignment + refusal of what cannot be acted on)\n");
    return 0;
}
