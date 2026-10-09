#pragma once
/** @file backend/cve_2026_43499/memory/heap_geometry.hpp
 *  The geometry the mechanism asks a device for, and the check that it got it.
 *
 *  Two kinds of number meet here. The order the mechanism prepares and the factor
 *  between a region and the buffer it sends are mechanism constants: they say what
 *  this exploit asks the kernel for, and they do not vary with the device. The
 *  kernel's page size does vary -- four kilobytes on some devices, sixteen on
 *  others -- so it is not a constant here at all: it is measured, and everything
 *  derived from it is checked against that measurement rather than assumed.
 *
 *  The retired tree assumed four kilobyte pages and built its sizes on that by
 *  hand, which is why the check below exists: a device with larger pages would have
 *  produced a region of the wrong size, and nothing would have said so.
 *
 *  Values: the order and the factor are the retired tree's own (its region was named
 *  after the order and its send was twice the region). They are mechanism, not
 *  tuning, so they belong in code; the page size is the only input.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/page_geometry.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** The order the mechanism prepares: its region is this many pages. */
    inline constexpr std::uint64_t kMmOrder = 3U;
    /** How many regions the shaping send carries. */
    inline constexpr std::uint64_t kSkbSendFactor = 2U;
    /** How many objects a region holds at most; the stride is derived from the sizes
     *  the profile declares, and this is only the bound the reclaim walk assumes. */
    inline constexpr std::uint64_t kMaxObjectsPerRegion = 1U << 16U;

    /** The region a device with this page size needs: the page shifted by the order. */
    [[nodiscard]] constexpr std::uint64_t region_for(std::uint64_t kernel_page_size) noexcept {
        if (kernel_page_size == 0U) {
            return 0U;
        }
        return kernel_page_size << kMmOrder;
    }

    /** The send size that region implies. */
    [[nodiscard]] constexpr std::uint64_t send_size_for(std::uint64_t region_size) noexcept {
        return region_size * kSkbSendFactor;
    }

    /** Why a geometry does not match the device. Names follow the single table. */
    enum class HeapGeometryVerdict : unsigned char { Ok = 0, GeometryMissing = 1 };

    [[nodiscard]] constexpr const char *heap_geometry_verdict_name(
        HeapGeometryVerdict verdict) noexcept {
        return verdict == HeapGeometryVerdict::Ok ? "ok" : "geometry-missing";
    }

    /** Check a region against the page size the device actually reports. A region that
     *  is not the one this order implies means the sizes were built for a page the
     *  device does not have, and that is refused rather than run: running it would
     *  work on the wrong region without anything looking wrong. */
    [[nodiscard]] constexpr HeapGeometryVerdict check_region_matches_device(
        std::uint64_t region_size, std::uint64_t kernel_page_size) noexcept {
        if (kernel_page_size == 0U || region_size == 0U) {
            return HeapGeometryVerdict::GeometryMissing;
        }
        return region_size == region_for(kernel_page_size) ? HeapGeometryVerdict::Ok
                                                           : HeapGeometryVerdict::GeometryMissing;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
