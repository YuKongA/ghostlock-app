#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "route/backend_contract.hpp"
#include "route/component_catalog.hpp"
#include "route/route_policy.hpp"
#include "session/exploit_session.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::runtime {
    /* Pipeline terminal outcome (Batch 4 review P3). Completed = the selected
     * pipeline ran to the end; DiagnosticStop = a stage decided the objective
     * was already met and stopped early (e.g. KernelSU already rooted);
     * Failed = a stage failed or violated its terminal contract; Rejected = no
     * catalogued combination owns the selection. `stage` names the last stage
     * that produced the outcome. Cleanup state is owned by the stages/session
     * and is deliberately not part of this result: the pipeline only reports
     * what its stages actually produced. */
    enum class RunCode {
        Completed,
        DiagnosticStop,
        Failed,
        Rejected,
    };

    enum class RunStage {
        None,
        Backend,
        Frontend,
    };

    struct RunResult final {
        RunCode code = RunCode::Failed;
        RunStage stage = RunStage::None;
    };

    /* Compile-time catalogue check for one pipeline instantiation. */
    template <class Frontend, class Backend, class Middleware>
    [[nodiscard]] constexpr bool pipeline_catalogued() noexcept {
        return combination_supported(
            ComponentSelection{Frontend::kind, Backend::kind, Middleware::kind});
    }

    /* Batch 4 pipeline composition. The type fixes one catalogued combination:
     * instantiating it for an uncatalogued tuple fails the static_assert, and
     * run() is the only execution entry, so the catalogue, the orchestrator
     * dispatch (component_catalog::dispatch_target) and this template cannot
     * drift apart. */
    template <class Frontend, class Backend, class Middleware>
    struct Pipeline final {
        static constexpr FrontendKind frontend = Frontend::kind;
        static constexpr BackendKind backend = Backend::kind;
        static constexpr MiddlewareKind middleware = Middleware::kind;
        static constexpr bool catalogued = pipeline_catalogued<Frontend, Backend, Middleware>();
        static_assert(catalogued, "pipeline must be a catalogued combination");
        static_assert(route::MiddlewarePolicy<Middleware>);
        static_assert(BackendExecution<Backend, Middleware>,
                      "backend must satisfy the execution contract for this middleware");

        /* The dispatch target this middleware policy is wired to; the
         * orchestrator asserts each case against it. */
        static constexpr DispatchTarget target = dispatch_target_of(Middleware::kind);
        static_assert(target != DispatchTarget::None);

        /* Backend steps first (Continue hands the chain on), then the frontend
         * handoff. Middleware hooks are direct static calls on the middleware
         * policy; no vtable or indirect dispatch enters the path. */
        [[nodiscard]] static RunResult run(session::ExploitSession &exploit_session,
                                           const profile::kernel_offsets &decoded,
                                           const char *debug_dir, bool force_attack) {
            session::VictimChain chain{};
            switch (Backend::template run<Middleware>(exploit_session, decoded, debug_dir,
                                                      force_attack, chain)) {
                case session::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Backend};
                case session::StageResult::Done:
                    return RunResult{.code = RunCode::DiagnosticStop, .stage = RunStage::Backend};
                case session::StageResult::Continue:
                    break;
            }
            switch (Frontend::run(exploit_session, chain)) {
                case session::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Frontend};
                case session::StageResult::Done:
                    return RunResult{.code = RunCode::Completed, .stage = RunStage::Frontend};
                case session::StageResult::Continue:
                    /* The frontend step is terminal; Continue is a contract
                     * violation and must not be silently read as success. */
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Frontend};
            }
            return RunResult{.code = RunCode::Failed, .stage = RunStage::Frontend};
        }
    };
} // namespace ghostlock::runtime

#endif
