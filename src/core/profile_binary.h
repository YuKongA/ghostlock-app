#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin's
 * NativeProfileDocument (app/src/main/kotlin/com/ghostlock/app/data/
 * NativeProfile.kt). Layout, version 3, little-endian:
 *
 *   u32 magic ("GLK1"), u16 version, u8 route, u8 kernel_major,
 *   u8 recommend_shizuku, u8 fallback_route, u16 release_length,
 *   release bytes (UTF-8), then a fixed array of int64 values in the exact
 *   order listed in profile_binary.cpp / NativeProfile.kt.
 *
 * The JSON decoder stays for assets validation, imports and host tests; the
 * runtime path uses this typed layout. */

#include "profile.h"

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace ghostlock::binary_profile {

inline constexpr uint32_t kMagic = 0x314B4C47u; /* "GLK1" */
inline constexpr uint16_t kVersion = 3u;

/* Parse one binary document into the native transport struct. */
int parse(std::string_view document, struct kernel_offsets *out,
        char *release_buf, size_t release_buf_cap);

/* Serialize the same layout (host tests and tooling). */
int serialize(const struct kernel_offsets *in, char *buffer, size_t capacity);

}  // namespace ghostlock::binary_profile

#endif
