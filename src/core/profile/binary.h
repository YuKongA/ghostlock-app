#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin's
 * NativeProfileDocument (app/src/main/kotlin/com/ghostlock/app/data/
 * NativeProfile.kt). Layout, little-endian:
 *
 *   u32 magic (0x0D000721), u16 version, u8 route, u8 kernel_major,
 *   u8 recommend_shizuku, u8 fallback_route, u16 release_length,
 *   release bytes (UTF-8), then a fixed array of int64 values in the exact
 *   order listed in profile_binary.cpp / NativeProfile.kt.
 *
 * v1 JSON profiles never reach this unit: imports are converted by the v1
 * converter, and the runtime path always uses this typed layout. */

#include "profile/model.h"

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace ghostlock::binary_profile {
    inline constexpr uint32_t kMagic = 0x0D000721u;
    inline constexpr uint16_t kVersion = 2u;

    /* Parse one binary document into the native transport struct. */
    int32_t parse(std::string_view document, struct ghostlock::profile::kernel_offsets *out,
              char *release_buf, size_t release_buf_cap);

    /* Serialize the same layout (host tests and tooling). */
    int32_t serialize(const struct ghostlock::profile::kernel_offsets *in, char *buffer, size_t capacity);
} // namespace ghostlock::binary_profile

#endif
