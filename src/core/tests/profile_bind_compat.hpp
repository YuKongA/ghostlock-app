#ifndef GHOSTLOCK_TESTS_PROFILE_BIND_COMPAT_HPP
#define GHOSTLOCK_TESTS_PROFILE_BIND_COMPAT_HPP

/* Host-test compatibility shim (A2-5).
 *
 * The production entry path is now framing-only (profile/binary.h) and the
 * owner binding lives in the selected backend. The host tests that pin the
 * legacy decode semantics (v2 golden vectors, v3 <-> v2 equivalence, 43284
 * private section) still call the old binary_profile::parse / parse_v3 /
 * bind_document names. This test-only unit recreates them as frame + owner
 * bind, so it exercises the same framing and Schema code the production
 * binding uses without putting a backend dependency back into profile/. */

#include "profile/binary.h"
#include "profile/document.hpp"
#include "contract/model.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend {
    struct Cve2026_43284Profile;
}

namespace ghostlock::binary_profile {
    int32_t parse(std::string_view document, ghostlock::profile::kernel_offsets *out,
                  char *release_buf, size_t release_buf_cap, component_ids *ids = nullptr,
                  ghostlock::profile::Document *document_out = nullptr,
                  ghostlock::backend::Cve2026_43284Profile *profile_43284_out = nullptr);

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
} // namespace ghostlock::binary_profile

#endif
