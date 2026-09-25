#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "route/component_catalog.hpp"
#include "route/route_policy.hpp"
#include "session/exploit_session.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::runtime {
    /* Batch 4 (D1=B) pipeline composition shape. It pins the three component
     * kinds of one selected pipeline at compile time; availability stays owned
     * by component_catalog (no second availability definition here). */
    template <class Frontend, class Backend, class Middleware>
    struct Pipeline final {
        static constexpr FrontendKind frontend = Frontend::kind;
        static constexpr BackendKind backend = Backend::kind;
        static constexpr MiddlewareKind middleware = Middleware::kind;
    };

    /* Pipeline terminal outcome (Batch 4 review P3). Completed = the selected
     * pipeline ran to the end; DiagnosticStop = a stage decided the objective
     * was already met and stopped early (e.g. KernelSU already rooted);
     * Failed = a stage failed; Rejected = no catalogued combination owns the
     * selection. `stage` names the last stage that produced the outcome and
     * `clean` is true when the run needs no further recovery. */
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
        bool clean = false;
    };

    /* A pipeline is dispatchable only when all three kinds are available. */
    template <class Frontend, class Backend, class Middleware>
    [[nodiscard]] constexpr bool pipeline_supported() noexcept {
        return frontend_available(Frontend::kind) &&
               backend_available(Backend::kind) &&
               middleware_available(Middleware::kind);
    }

    /* Run one catalogued pipeline. The backend policy owns the write-stage
     * sequence (Continue hands the filled chain to the frontend), the frontend
     * policy owns the handoff step, and the middleware policy is passed into
     * the backend steps as a type: every route hook (resident_write, repairs)
     * is a direct static call on `Middleware`, so different middleware
     * instances produce different pipeline code by construction - no vtable or
     * indirect dispatch enters the path. */
    template <class Frontend, class Backend, class Middleware>
    [[nodiscard]] RunResult run_pipeline(session::ExploitSession &exploit_session,
                                         const profile::kernel_offsets &decoded,
                                         const char *debug_dir, bool force_attack) {
        static_assert(pipeline_supported<Frontend, Backend, Middleware>());
        static_assert(route::MiddlewarePolicy<Middleware>);
        session::VictimChain chain{};
        switch (Backend::template run<Middleware>(exploit_session, decoded, debug_dir,
                                                  force_attack, chain)) {
            case session::StageResult::Failed:
                return RunResult{.code = RunCode::Failed, .stage = RunStage::Backend,
                                 .clean = false};
            case session::StageResult::Done:
                return RunResult{.code = RunCode::DiagnosticStop, .stage = RunStage::Backend,
                                 .clean = true};
            case session::StageResult::Continue:
                break;
        }
        return Frontend::run(exploit_session, chain) == session::StageResult::Failed
                   ? RunResult{.code = RunCode::Failed, .stage = RunStage::Frontend,
                               .clean = false}
                   : RunResult{.code = RunCode::Completed, .stage = RunStage::Frontend,
                               .clean = true};
    }
} // namespace ghostlock::runtime

#endif
