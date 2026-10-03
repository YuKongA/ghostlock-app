#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin
 * (profile-core/.../NativeProfile.kt). Little-endian.
 *
 * v2 (the only format; object sections):
 *   u32 magic, u16 version(2), u16 terminal_id, u16 backend_id,
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

namespace ghostlock::profile {
    struct Document;
}

namespace ghostlock::backend {
    struct Cve2026_43284Profile;
}

namespace ghostlock::binary_profile {
    inline constexpr uint32_t kMagic = 0x0D000721u;
    inline constexpr uint16_t kVersion = 2u;
    /* Known component ids. The full catalog is decoded here; an id that is
     * known but unavailable (UMH / cve_2026_64560 / 31431 / 43503 / 23274 /
     * 43284) is accepted at decode time and rejected by the orchestrator
     * before the attack starts. */
    inline constexpr uint16_t kTerminalRootChild = 1u;
    inline constexpr uint16_t kTerminalUmhForward = 2u;
    inline constexpr uint16_t kBackendCve202643499 = 1u;
    inline constexpr uint16_t kBackendCve20264560 = 2u;
    inline constexpr uint16_t kBackendCve202631431 = 3u;
    inline constexpr uint16_t kBackendCve202643503 = 4u;
    inline constexpr uint16_t kBackendCve202623274 = 5u;
    inline constexpr uint16_t kBackendCve202643284 = 6u;

    /* StepSet ids (ADR-0004 R18); carried in the backend's private section
     * (`backend.cve_2026_43499`, field `steps`), never in the header. */
    inline constexpr uint16_t kStepSetW1W2 = 1u;
    inline constexpr uint16_t kStepSetW1W3 = 2u;
    inline constexpr uint16_t kStepSetPageCacheWrite = 3u;

    [[nodiscard]] constexpr bool stepset_known(uint16_t id) noexcept {
        return id == kStepSetW1W2 || id == kStepSetW1W3 || id == kStepSetPageCacheWrite;
    }

    [[nodiscard]] constexpr bool terminal_known(uint16_t id) noexcept {
        return id == kTerminalRootChild || id == kTerminalUmhForward;
    }

    [[nodiscard]] constexpr bool backend_known(uint16_t id) noexcept {
        return id == kBackendCve202643499 || id == kBackendCve20264560 ||
               id == kBackendCve202631431 || id == kBackendCve202643503 ||
               id == kBackendCve202623274 || id == kBackendCve202643284;
    }

    /* Component selection as decoded from the transport. v2 fills the single
     * shipped terminal/backend; v3 carries the wire ids. Kept out of
     * kernel_offsets so the execution struct layout (and attack codegen) does
     * not move. */
    struct component_ids {
        uint16_t terminal;
        uint16_t backend;
        uint16_t middleware;
        /* 0 = absent; the native selection rejects a missing/unknown StepSet
         * rather than defaulting (R18). */
        uint16_t steps;
    };

    /* Parse one binary document into the native transport struct. `ids`, when
     * given, receives the decoded component selection. `document_out`, when
     * given, additionally receives the neutral framing result (shadow path,
     * A2-3c-1); it never changes the transport decode or its return value.
     *
     * `profile_43284_out`, when given, receives the bound
     * backend.cve_2026_43284 private View (S3 B4). The section is accepted only
     * when the header backend id is kBackendCve202643284; for every other
     * backend it is an unknown section and Production rejects the document. The
     * View is not a CoreSession slot and never changes the 43499 layout. */
    int32_t parse(std::string_view document, struct ghostlock::profile::kernel_offsets *out,
              char *release_buf, size_t release_buf_cap, component_ids *ids = nullptr,
              struct ghostlock::profile::Document *document_out = nullptr,
              struct ghostlock::backend::Cve2026_43284Profile *profile_43284_out = nullptr);

    /* Serialize the same layout (host tests and tooling). */
    int32_t serialize(const struct ghostlock::profile::kernel_offsets *in, char *buffer, size_t capacity);
} // namespace ghostlock::binary_profile

#endif
