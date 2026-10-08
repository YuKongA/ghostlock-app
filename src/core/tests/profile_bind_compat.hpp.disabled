#ifndef GHOSTLOCK_TESTS_PROFILE_BIND_COMPAT_HPP
#define GHOSTLOCK_TESTS_PROFILE_BIND_COMPAT_HPP

/* Host-test compatibility shim (A2-5).
 *
 * The production entry path is framing-only (profile/glkv3_parse.hpp) and the
 * owner binding lives in the selected backend. The host tests that pin the
 * GLKv3 decode + owner-bind semantics still call the old parse_v3 /
 * bind_document names. This test-only unit recreates them as frame + owner
 * bind, so it exercises the same framing and Schema code the production
 * binding uses without putting a backend dependency back into profile/.
 * The v2 parse entry was removed in S4 R2c with the v2 wire; the numeric
 * selection vocabulary the vectors compare is now test-local and derived from
 * the contract kinds, so no production header has to carry it. */

#include "contract/identity.hpp"
#include "contract/model.hpp"
#include "profile/document.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend {
    struct Cve2026_43284Profile;
}

namespace ghostlock::tests::profile_bind {
    /* Component selection as resolved by the composition root, in the numeric
     * form the frozen transport uses (mirrors contract::*Kind's values). */
    struct component_ids {
        uint16_t terminal;
        uint16_t backend;
        uint16_t middleware;
        /* 0 = absent; the native selection rejects a missing/unknown StepSet
         * rather than defaulting (R18). */
        uint16_t steps;
    };

    [[nodiscard]] constexpr bool terminal_known(uint16_t id) noexcept {
        return id == static_cast<uint16_t>(contract::TerminalKind::RootChild) ||
               id == static_cast<uint16_t>(contract::TerminalKind::UmhForward);
    }

    [[nodiscard]] constexpr bool backend_known(uint16_t id) noexcept {
        switch (static_cast<contract::BackendKind>(id)) {
            case contract::BackendKind::Cve2026_43499:
            case contract::BackendKind::Cve2026_64560:
            case contract::BackendKind::Cve2026_31431:
            case contract::BackendKind::Cve2026_43503:
            case contract::BackendKind::Cve2026_23274:
            case contract::BackendKind::Cve2026_43284: return true;
        }
        return false;
    }

    int32_t parse_v3(std::string_view document, ghostlock::profile::kernel_offsets *out,
                     char *release_buf, size_t release_buf_cap,
                     component_ids *ids = nullptr,
                     ghostlock::profile::Document *document_out = nullptr,
                     ghostlock::backend::Cve2026_43284Profile *profile_43284_out = nullptr);

    int32_t bind_document(ghostlock::profile::Document &&document, uint8_t route,
                          uint16_t terminal, uint16_t backend, uint16_t middleware,
                          ghostlock::profile::kernel_offsets *out, char *release_buf,
                          size_t release_buf_cap, component_ids *ids = nullptr,
                          ghostlock::profile::Document *document_out = nullptr,
                          ghostlock::backend::Cve2026_43284Profile *profile_43284_out = nullptr);
} // namespace ghostlock::tests::profile_bind

#endif
