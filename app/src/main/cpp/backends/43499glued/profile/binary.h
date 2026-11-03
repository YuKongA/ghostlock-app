#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin
 * (profile-core/.../NativeProfile.kt). Little-endian.
 *
 * v2 (the only format; object sections):
 *   u32 magic, u16 version(2), u16 frontend_id, u16 backend_id,
 *   u16 middleware_id, u16 release_length, release,
 *   u16 section_count, then per section:
 *     u8 name_len, name, u32 entry_count, then per entry:
 *       u8 key_len, key, u64 value
 * Presence is carried by key occurrence: an omitted field means "not
 * provided", so a provided 0 is distinct from an absent one. Values are stored
 * bit-exactly (signed via two's complement). There is no positional layout, so
 * adding a field or an object never moves anything else.
 *
 * v1 JSON profiles never reach this unit: imports are converted by Kotlin's
 * LegacyProfileConverter, and the runtime path always uses this typed layout. */

#include "profile/model.h"

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace ghostlock::binary_profile {
    inline constexpr uint32_t kMagic = 0x0D000721u;
    inline constexpr uint16_t kVersion = 2u;
    /* Known component ids. The full catalog is decoded here; an id that is
     * known but unavailable (UMH / cve_2026_64560) is accepted at decode time
     * and rejected by the orchestrator before the attack starts. */
    inline constexpr uint16_t kFrontendRootChild = 1u;
    inline constexpr uint16_t kFrontendUmhForward = 2u;
    inline constexpr uint16_t kBackendCve202643499 = 1u;
    inline constexpr uint16_t kBackendCve20264560 = 2u;

    [[nodiscard]] constexpr bool frontend_known(uint16_t id) noexcept {
        return id == kFrontendRootChild || id == kFrontendUmhForward;
    }

    [[nodiscard]] constexpr bool backend_known(uint16_t id) noexcept {
        return id == kBackendCve202643499 || id == kBackendCve20264560;
    }

    /* Component selection as decoded from the transport. v2 fills the single
     * shipped frontend/backend; v3 carries the wire ids. Kept out of
     * kernel_offsets so the execution struct layout (and attack codegen) does
     * not move. */
    struct component_ids {
        uint16_t frontend;
        uint16_t backend;
        uint16_t middleware;
    };

    /* Parse one binary document into the native transport struct. `ids`, when
     * given, receives the decoded component selection. */
    int32_t parse(std::string_view document, struct ghostlock::profile::kernel_offsets *out,
              char *release_buf, size_t release_buf_cap, component_ids *ids = nullptr);

    /* Serialize the same layout (host tests and tooling). */
    int32_t serialize(const struct ghostlock::profile::kernel_offsets *in, char *buffer, size_t capacity);
} // namespace ghostlock::binary_profile

#endif
