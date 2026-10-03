#ifndef GHOSTLOCK_BACKEND_POLICY_HPP
#define GHOSTLOCK_BACKEND_POLICY_HPP

#include "pipeline/component_catalog.hpp"

namespace ghostlock::pipeline::backend {
    /* Batch 4 DECLARATION-ONLY backend scaffolding (D2 shallow placeholder).
     * These structs name the known backend ids; they expose no execution path
     * and no availability logic. Availability is owned by
     * component_catalog::backend_available(), and the static_asserts below fail
     * to compile if this declaration drifts from it. The cve_2026_64560/31431/
     * 43503/23274/43284 ids are known but unavailable (rejected before the
     * attack by the orchestrator). */

    struct Cve2026_43499 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_43499;
    };

    struct Cve2026_64560 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_64560;
    };

    struct Cve2026_31431 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_31431;
    };

    struct Cve2026_43503 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_43503;
    };

    struct Cve2026_23274 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_23274;
    };

    struct Cve2026_43284 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_43284;
    };

    static_assert(backend_available(Cve2026_43499::kind));
    static_assert(!backend_available(Cve2026_64560::kind));
    static_assert(!backend_available(Cve2026_31431::kind));
    static_assert(!backend_available(Cve2026_43503::kind));
    static_assert(!backend_available(Cve2026_23274::kind));
    static_assert(!backend_available(Cve2026_43284::kind));
} // namespace ghostlock::pipeline::backend

#endif
