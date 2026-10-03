#ifndef GHOSTLOCK_HOST_MEMORY_DIRECT_MAP_HPP
#define GHOSTLOCK_HOST_MEMORY_DIRECT_MAP_HPP

/* Host shadow of memory/direct_map.hpp: the data-flow test uses fake task
 * addresses, so every address counts as in-map. */

#include <cstdint>

namespace ghostlock::memory {
    inline std::int32_t in_direct_map(std::uintptr_t) noexcept { return 1; }
} // namespace ghostlock::memory

#endif
