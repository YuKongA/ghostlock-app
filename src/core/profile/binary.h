#ifndef GHOSTLOCK_PROFILE_BINARY_H
#define GHOSTLOCK_PROFILE_BINARY_H

/* Binary transport for the resolved profile, shared with Kotlin
 * (profile-core/.../NativeProfile.kt). Little-endian.
 *
 * v2 (legacy read path; the branch writes GLKv3, see profile/glkv3_parse.hpp.
 * Object sections):
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
 * This unit is framing only (A2-5): it turns the wire into the neutral
 * profile::Document without naming a field or knowing an owner. The owner
 * binding lives in the selected backend (backend/cve_2026_43499/backend_profile
 * and backend/cve_2026_43284). v1 JSON profiles never reach this unit: imports
 * are converted by Kotlin's LegacyProfileConverter. */

#include "profile/document.hpp"

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace ghostlock::profile {
    /* Defined in contract/model.hpp; the v2 writer declaration below only needs the
     * name so this framing header stays independent of the frozen ABI type. */
    struct kernel_offsets;
} // namespace ghostlock::profile

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
     * not move. Retained as a transport-level selection value; the production
     * read path derives its selection from the Document. */
    struct component_ids {
        uint16_t terminal;
        uint16_t backend;
        uint16_t middleware;
        /* 0 = absent; the native selection rejects a missing/unknown StepSet
         * rather than defaulting (R18). */
        uint16_t steps;
    };

    /* Frame one v2 document into the neutral profile::Document. Validates the
     * magic/version, the known component ids and the route selection (an
     * unresolved route is rejected, except the route-less 43284 backend); it
     * never names a field and never binds an owner. */
    int32_t frame(std::string_view document, struct ghostlock::profile::Document *out);

    /* v2 writer: golden/equivalence host tests only. Production and export
     * write GLKv3 (GLKv3-4) and never call this; v2 is read-only there. The
     * macro is defined only for the host test build (src/Makefile
     * HOST_CXXFLAGS), so the device object carries no v2 serializer. */
#if defined(GHOSTLOCK_ENABLE_V2_WRITER)
    int32_t serialize(const struct ghostlock::profile::kernel_offsets *in, char *buffer, size_t capacity);
#endif
} // namespace ghostlock::binary_profile

#endif
