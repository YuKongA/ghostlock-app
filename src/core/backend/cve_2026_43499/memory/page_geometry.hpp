#pragma once
/** @file backend/cve_2026_43499/memory/page_geometry.hpp
 *  The geometry a page preparation is given, and the two decisions that need it.
 *
 *  Nothing here owns a number. The page size, the send size and the socket buffer
 *  are inputs, because the first is a kernel geometry and the others are the
 *  profile's; this file only decides what follows from them, which is arithmetic and
 *  a check. That is what makes it testable without a kernel and reviewable without a
 *  device.
 *
 *  The two decisions are the ones the retired tree made inline: rounding a leaked
 *  address down to the page it starts in, and checking that the geometry it was
 *  handed is one it can act on. A geometry that cannot be acted on is refused by
 *  name rather than patched up here, because silently rounding a bad page size would
 *  produce an address that belongs to no page at all.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** What a preparation needs to know about the memory it is working in. */
    struct PageGeometry final {
        /** The size of the region the alignment rounds down to (a power of two). */
        std::uint64_t page_size = 0U;
        /** How many bytes the shaping send carries. */
        std::size_t send_size = 0U;
        /** The socket buffer the shaping pair is given. */
        std::int32_t socket_buffer = 0;
    };

    /** Why a geometry cannot be acted on. Names follow the single table. */
    enum class GeometryVerdict : unsigned char { Ok = 0, GeometryInvalid = 1 };

    [[nodiscard]] constexpr const char *geometry_verdict_name(GeometryVerdict verdict) noexcept {
        return verdict == GeometryVerdict::Ok ? "ok" : "param-invalid";
    }

    /** True when the geometry is one a preparation can act on: the page size has to
     *  be a power of two (or the mask below picks up bits that are not a page), and
     *  the two sizes have to be non-zero. */
    [[nodiscard]] constexpr bool geometry_is_usable(const PageGeometry &geometry) noexcept {
        if (geometry.page_size == 0U || geometry.send_size == 0U ||
            geometry.socket_buffer <= 0) {
            return false;
        }
        return (geometry.page_size & (geometry.page_size - 1U)) == 0U;
    }

    /** Round a leaked address down to the start of the region it belongs to. */
    [[nodiscard]] constexpr std::uint64_t align_base(std::uint64_t leaked,
                                                     std::uint64_t page_size) noexcept {
        if (page_size == 0U || (page_size & (page_size - 1U)) != 0U) {
            return 0U;
        }
        return leaked & ~(page_size - 1U);
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
