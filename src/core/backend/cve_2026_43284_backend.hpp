#ifndef GHOSTLOCK_CVE2026_43284_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43284_BACKEND_HPP

#include <string_view>

#include "pipeline/backend_contract.hpp"
#include "pipeline/component_catalog.hpp"

namespace ghostlock::backend {
    /* CVE-2026-43284 backend placeholder (Batch B3). A known-but-unavailable
     * id: no execution path, no routes, no device offsets and no profile
     * fields. The wire carries only the backend id for now, so the schema is
     * intentionally empty until the vulnerability work lands; any future fields
     * must live in this backend's own section and must never reuse the
     * cve_2026_43499 slots.
     *
     * Availability is owned by component_catalog::backend_available(); this
     * type only carries the stable id, the identity contract and the reason. */
    struct Cve2026_43284Policy final {
        static constexpr pipeline::BackendKind kind = pipeline::BackendKind::Cve2026_43284;
        static constexpr std::string_view unavailable_reason =
            "cve_2026_43284 backend is not implemented";
    };

    static_assert(pipeline::BackendIdentity<Cve2026_43284Policy>);
    static_assert(!pipeline::backend_available(Cve2026_43284Policy::kind));
    static_assert(Cve2026_43284Policy::kind == pipeline::backend::Cve2026_43284::kind);
} // namespace ghostlock::backend

#endif
