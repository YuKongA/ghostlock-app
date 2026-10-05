#ifndef GHOSTLOCK_CVE2026_43503_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43503_BACKEND_HPP

#include <string_view>

#include "contract/identity.hpp"

namespace ghostlock::backend {
    /* CVE-2026-43503 backend placeholder. Upstream: "net: skbuff: propagate
     * shared-frag marker through frag-transfer helpers" (missing
     * SKBFL_SHARED_FRAG propagation; ESP authencesn-ESN stray writes reach the
     * page cache of a root-owned read-only file), a different primitive from
     * the cve_2026_43499 PI-futex path. A known-but-unavailable id: no execution
     * path, no device offsets and no profile fields. Any future fields must
     * live in this backend's own wire section and must never reuse the
     * cve_2026_43499 slots.
     *
     * Availability is owned by contract::backend_available(); this
     * type only carries the stable id, the identity contract and the reason. */
    struct Cve2026_43503Policy final {
        static constexpr contract::BackendKind kind = contract::BackendKind::Cve2026_43503;
        static constexpr std::string_view unavailable_reason =
            "cve_2026_43503 backend is not implemented";
    };

    static_assert(contract::BackendIdentity<Cve2026_43503Policy>);
    static_assert(!contract::backend_available(Cve2026_43503Policy::kind));
    static_assert(Cve2026_43503Policy::kind == contract::backend::Cve2026_43503::kind);
} // namespace ghostlock::backend

#endif
