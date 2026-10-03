#ifndef GHOSTLOCK_CVE2026_31431_BACKEND_HPP
#define GHOSTLOCK_CVE2026_31431_BACKEND_HPP

#include <string_view>

#include "pipeline/backend_contract.hpp"
#include "pipeline/component_catalog.hpp"

namespace ghostlock::backend {
    /* CVE-2026-31431 backend placeholder. Upstream: "crypto: algif_aead - Revert
     * to operating out-of-place" (AF_ALG AEAD), a different primitive from the
     * cve_2026_43499 PI-futex path. A known-but-unavailable id: no execution
     * path, no device offsets and no profile fields. Any future fields must
     * live in this backend's own wire section and must never reuse the
     * cve_2026_43499 slots.
     *
     * Availability is owned by component_catalog::backend_available(); this
     * type only carries the stable id, the identity contract and the reason. */
    struct Cve2026_31431Policy final {
        static constexpr pipeline::BackendKind kind = pipeline::BackendKind::Cve2026_31431;
        static constexpr std::string_view unavailable_reason =
            "cve_2026_31431 backend is not implemented";
    };

    static_assert(pipeline::BackendIdentity<Cve2026_31431Policy>);
    static_assert(!pipeline::backend_available(Cve2026_31431Policy::kind));
    static_assert(Cve2026_31431Policy::kind == pipeline::backend::Cve2026_31431::kind);
} // namespace ghostlock::backend

#endif
