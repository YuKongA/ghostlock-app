#ifndef GHOSTLOCK_ORCHESTRATOR_HPP
#define GHOSTLOCK_ORCHESTRATOR_HPP

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_43499/backend_profile.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "session/core_session.hpp"
#include "backend/cve_2026_43499/terminal/root_child.hpp"
#include "terminal/umh_forward.hpp"

#include <cstdio>

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
    /* S4 R6b: the step set is no longer a user-selectable id; it is derived
     * from the resolved combination token (profile/glkv3_parse.cpp). */
    [[nodiscard]] inline contract::StepSetKind wire_stepset(const profile::Document &document) {
        const contract::CombinationSpec *spec = contract::combination_spec(
                static_cast<contract::CombinationKind>(document.combination));
        return spec != nullptr ? spec->steps : contract::StepSetKind::Unknown;
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
        /* S4 R6b: the token is the composition authority; a planned token
         * (known but available=false) is rejected here before dispatch. */
        const contract::CombinationKind combination =
                static_cast<contract::CombinationKind>(document.combination);
        if (combination == contract::CombinationKind::Unknown ||
            !contract::combination_available(combination)) {
            const std::string_view token = contract::combination_name(combination);
            (void)std::fprintf(stderr, "unsupported combination token=%.*s\n",
                               static_cast<int>(token.size()), token.data());
            return RunResult{.code = RunCode::Rejected};
        }
        /* PI-window-outside: each Pipeline::run constructs/destroys the selected
         * backend's state through the BackendState RAII guard (ADR-0002 / D3),
         * so the composition root no longer names 43499 or its state. */
        switch (dispatch_target_of(selection.backend, combination)) {
            case DispatchTarget::Cve43499W1W3_RootChild: {
                using P = Pipeline<ghostlock::backend::Cve2026_43499Policy,
                                   ghostlock::backend::cve_2026_43499::terminal::RootChildPolicy>;
                static_assert(P::target == DispatchTarget::Cve43499W1W3_RootChild,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, document, debug_dir, force_attack);
            }
            case DispatchTarget::Cve43499W1W2_RootChild: {
                using P = Pipeline<ghostlock::backend::Cve43499_W1W2,
                                   ghostlock::backend::cve_2026_43499::terminal::RootChildPolicy>;
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
                /* Named diagnostic (step-queue design doc 10.1-S5): this branch
                 * used to return Rejected without printing anything, so a legal
                 * but unwired plan looked like "nothing happened". M1 adds the
                 * diagnostic only -- the dispatch logic is unchanged; the message
                 * itself is pinned by component_catalog_test. */
                (void)std::fprintf(stderr, "%s\n",
                                   no_dispatch_target_message(selection.backend,
                                                              combination)
                                           .c_str());
                return RunResult{.code = RunCode::Rejected};
        }
        return RunResult{.code = RunCode::Rejected};
    }
} // namespace ghostlock::pipeline

#endif
