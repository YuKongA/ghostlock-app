#ifndef GHOSTLOCK_CVE2026_64560_BACKEND_HPP
#define GHOSTLOCK_CVE2026_64560_BACKEND_HPP

#include <string_view>

#include "route/backend_contract.hpp"
#include "route/component_catalog.hpp"

namespace ghostlock::session::backend {
    /* CVE-2026-64560 backend placeholder (Batch 5). A known-but-unavailable id:
     * no execution path, no device offsets and no profile fields. The wire v3
     * carries only the backend id (there is no backend-private section), so the
     * schema is intentionally empty until the vulnerability work lands; any
     * future fields must live in this backend's own section and must never
     * reuse the cve_2026_43499 slots.
     *
     * Availability is owned by component_catalog::backend_available(); this
     * type only carries the stable id, the identity contract and the reason. */
    struct Cve2026_64560Policy final {
        static constexpr runtime::BackendKind kind = runtime::BackendKind::Cve2026_64560;
        static constexpr std::string_view unavailable_reason =
            "cve_2026_64560 backend is not implemented";
    };

    static_assert(runtime::BackendIdentity<Cve2026_64560Policy>);
    static_assert(!runtime::backend_available(Cve2026_64560Policy::kind));
    static_assert(Cve2026_64560Policy::kind == runtime::backend::Cve2026_64560::kind);
} // namespace ghostlock::session::backend

#endif
