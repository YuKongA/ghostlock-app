#ifndef GHOSTLOCK_MEMORY_DIRECT_MAP_HPP
#define GHOSTLOCK_MEMORY_DIRECT_MAP_HPP

#include "memory/target.h"

#include <cstdint>

namespace ghostlock::memory {
    /* Direct-map end, built-in or narrowed by a measured iomem dump (defined in
     * memory/address_space.cpp). */
    extern std::uint64_t g_direct_map_end;

    [[nodiscard]] inline std::int32_t in_direct_map(std::uintptr_t target) noexcept {
        return target > DIRECT_MAP_BASE && target < g_direct_map_end;
    }
} // namespace ghostlock::memory

#endif
