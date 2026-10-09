#pragma once
/** @file backend/cve_2026_43499/leak/snitch_region.hpp
 *  What the collision search shares between its processes, and how big it is.
 *
 *  The search forks, so the parent and its children read and write the same fields in
 *  the same order at the same offsets. That makes this layout an interface between
 *  processes, which is the kind of thing that breaks silently: a field added in the
 *  middle shifts everything after it, and the two sides disagree about where to look
 *  without either of them being wrong in its own terms. So the order is written down
 *  as data with a pin, the sizes are derived from the counts the search was given, and
 *  anything that does not add up is refused rather than mapped.
 *
 *  The region is one mapping: a header of fixed fields, then three arrays whose
 *  lengths follow from the collision and thread counts, then a page for the futex the
 *  parent waits on. Nothing here maps or forks anything -- it says what would be mapped,
 *  and the seam does the rest.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** The parts of the shared region, in the order they appear. */
    enum class RegionPart : unsigned char {
        /** The fixed fields: counts, the hash context, the results. */
        Header = 0,
        /** One address per collision, and one more as the original the others match. */
        FutexAddresses = 1,
        /** One timestamp per collision, for the search's own bookkeeping. */
        Timestamps = 2,
        /** One thread handle per searching thread. */
        ThreadHandles = 3,
        /** The page the parent waits on. */
        WaitPage = 4,
    };

    /** The parts, in order. A part inserted anywhere but the end is a new interface. */
    inline constexpr RegionPart kRegionOrder[] = {
        RegionPart::Header,        RegionPart::FutexAddresses, RegionPart::Timestamps,
        RegionPart::ThreadHandles, RegionPart::WaitPage,
    };
    inline constexpr std::size_t kRegionPartCount = sizeof(kRegionOrder) / sizeof(kRegionOrder[0]);

    [[nodiscard]] constexpr bool region_order_is_complete() noexcept {
        for (std::size_t i = 0U; i < kRegionPartCount; ++i) {
            std::size_t seen = 0U;
            for (std::size_t j = 0U; j < kRegionPartCount; ++j) {
                if (kRegionOrder[j] == kRegionOrder[i]) {
                    ++seen;
                }
            }
            if (seen != 1U) {
                return false;
            }
        }
        return kRegionPartCount == 5U;
    }
    static_assert(region_order_is_complete(),
                  "the shared region is five parts, each named once");

    /** What the region is sized from. */
    struct RegionInputs final {
        /** The fixed part, which the caller measures from the header it declares. */
        std::size_t header_bytes = 0U;
        /** How many collisions the search looks for. */
        std::size_t collisions = 0U;
        /** How many threads search. */
        std::size_t thread_count = 0U;
        /** The page the parent waits on. */
        std::size_t page_size = 0U;
        /** The width of one address or one timestamp in the region. */
        std::size_t word_bytes = 0U;
        /** The width of one thread handle. */
        std::size_t handle_bytes = 0U;
    };

    /** Where each part starts, and how large the whole region is. */
    struct RegionLayout final {
        std::size_t offsets[kRegionPartCount] = {};
        std::size_t bytes[kRegionPartCount] = {};
        std::size_t total_bytes = 0U;
    };

    /** Why a region cannot be laid out. Names follow the single table. */
    enum class RegionVerdict : unsigned char { Ok = 0, GeometryMissing = 1 };

    [[nodiscard]] constexpr const char *region_verdict_name(RegionVerdict verdict) noexcept {
        return verdict == RegionVerdict::Ok ? "ok" : "geometry-missing";
    }

    /** Lay the region out. The address array has one slot more than there are collisions,
     *  because the first collided address is the one the others are compared against. */
    [[nodiscard]] constexpr RegionVerdict layout_snitch_region(const RegionInputs &inputs,
                                                              RegionLayout &out) noexcept {
        out = RegionLayout{};
        if (inputs.header_bytes == 0U || inputs.collisions == 0U || inputs.thread_count == 0U ||
            inputs.page_size == 0U || inputs.word_bytes == 0U || inputs.handle_bytes == 0U) {
            return RegionVerdict::GeometryMissing;
        }
        const std::size_t address_slots = inputs.collisions + 1U;
        if (address_slots < inputs.collisions) {
            return RegionVerdict::GeometryMissing;
        }
        const std::size_t address_bytes = address_slots * inputs.word_bytes;
        const std::size_t time_bytes = inputs.collisions * inputs.word_bytes;
        const std::size_t handle_bytes = inputs.thread_count * inputs.handle_bytes;
        if (address_bytes / inputs.word_bytes != address_slots ||
            time_bytes / inputs.word_bytes != inputs.collisions ||
            handle_bytes / inputs.handle_bytes != inputs.thread_count) {
            return RegionVerdict::GeometryMissing;
        }
        const std::size_t parts[kRegionPartCount] = {inputs.header_bytes, address_bytes, time_bytes,
                                                     handle_bytes, inputs.page_size};
        std::size_t at = 0U;
        for (std::size_t i = 0U; i < kRegionPartCount; ++i) {
            if (parts[i] == 0U || at > (SIZE_MAX - parts[i])) {
                return RegionVerdict::GeometryMissing;
            }
            out.offsets[i] = at;
            out.bytes[i] = parts[i];
            at += parts[i];
        }
        out.total_bytes = at;
        return RegionVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
