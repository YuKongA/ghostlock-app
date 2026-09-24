#ifndef GHOSTLOCK_BACKEND_POLICY_HPP
#define GHOSTLOCK_BACKEND_POLICY_HPP

#include "route/component_catalog.hpp"

namespace ghostlock::runtime::backend {
    /* Batch 4 DECLARATION-ONLY backend scaffolding (D2 shallow placeholder).
     * These structs name the known backend ids; they expose no execution path
     * and no availability logic. Availability is owned by
     * component_catalog::backend_available(), and the static_asserts below fail
     * to compile if this declaration drifts from it. cve_2026_64560 is a known
     * but unavailable id (rejected before the attack by the orchestrator). */

    struct Cve2026_43499 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_43499;
    };

    struct Cve2026_64560 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_64560;
    };

    static_assert(backend_available(Cve2026_43499::kind));
    static_assert(!backend_available(Cve2026_64560::kind));
} // namespace ghostlock::runtime::backend

#endif
