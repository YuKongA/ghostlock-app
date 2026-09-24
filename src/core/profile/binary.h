#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin's
 * RuntimeCodec (profile-core/.../profile/). Little-endian.
 *
 * v2 (legacy, still decoded):
 *   u32 magic, u16 version(2), u8 route, u8 kernel_major, u8 recommend_shizuku,
 *   u8 fallback_route, u16 release_length, release, 68×u64 common slots,
 *   route section (u8 count + entries).
 *
 * v3 (current writer):
 *   u32 magic, u16 version(3), u16 frontend_id, u16 backend_id,
 *   u16 middleware_id, u8 kernel_major, u8 fallback_route, u16 release_length,
 *   release, 68×u64 core slots, middleware section (u16 count + entries),
 *   options section (u16 count + entries: safe_mode, selected_cpus.*).
 *
 * v3 drops the `recommend_shizuku` byte (App-only; the executor never reads it).
 * v1 JSON profiles never reach this unit: imports are converted by the v1
 * converter, and the runtime path always uses this typed layout. */

#include "profile/model.h"

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace ghostlock::binary_profile {
    inline constexpr uint32_t kMagic = 0x0D000721u;
    inline constexpr uint16_t kVersionV2 = 2u;
    inline constexpr uint16_t kVersionV3 = 3u;
    /* Version emitted by serialize(); parse() accepts both V2 and V3. */
    inline constexpr uint16_t kVersion = kVersionV3;
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
