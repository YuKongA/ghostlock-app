#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "profile/model.h"
#include "pipeline/backend_contract.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/terminal_contract.hpp"
#include "session/core_session.hpp"
#include "session/stage_types.hpp"
#include "terminal/rooted_child.hpp"

namespace ghostlock::pipeline {
    enum class RunCode { Completed, DiagnosticStop, Failed, Rejected };

    enum class RunStage { None, Backend, Terminal };

    struct RunResult final {
        RunCode code = RunCode::Failed;
        RunStage stage = RunStage::None;
    };

    namespace detail {
        /* B2 RAII binding for the optional backend state slot (ADR-0002 / D3):
         * the backend constructs on entry, the guard destroys on every exit
         * path, including all early returns below. A stateless backend omits
         * the contract and both branches are compiled out. */
        template <class Backend>
        struct BackendStateGuard final {
            session::CoreSession &session;

            explicit BackendStateGuard(session::CoreSession &exploit_session) noexcept
                : session(exploit_session) {
                if constexpr (BackendState<Backend>) {
                    Backend::state_construct(session);
                }
            }

            BackendStateGuard(const BackendStateGuard &) = delete;

            BackendStateGuard &operator=(const BackendStateGuard &) = delete;

            ~BackendStateGuard() noexcept {
                if constexpr (BackendState<Backend>) {
                    Backend::state_destroy(session);
                }
            }
        };
    } // namespace detail

    /* ADR-0004 R12: two assembly axes. The backend runs first and hands a
     * neutral ghostlock::terminal::RootedChild to the terminal; route is chosen inside the
     * backend, so it is not a template parameter. */
    template <class Backend, class Terminal>
    struct Pipeline final {
        static constexpr BackendKind backend = Backend::kind;
        static constexpr StepSetKind steps = Backend::steps;
        static constexpr TerminalKind terminal = Terminal::kind;
        static constexpr DispatchTarget target = dispatch_target_of(backend, steps, terminal);
        static_assert(target != DispatchTarget::None,
                      "pipeline must be a catalogued (backend, steps, terminal) triple");
        static_assert(TerminalExecution<Terminal>,
                      "terminal must satisfy the terminal execution contract");
        static_assert(BackendExecution<Backend, typename Terminal::Input>,
                      "backend must satisfy the execution contract for the terminal input");

        [[nodiscard]] static RunResult run(session::CoreSession &exploit_session,
                                           const profile::kernel_offsets &decoded,
                                           const char *debug_dir, bool force_attack) {
            const detail::BackendStateGuard<Backend> state_guard(exploit_session);
            typename Terminal::Input input{};
            switch (Backend::run(exploit_session, decoded, debug_dir, force_attack, input)) {
                case session::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Backend};
                case session::StageResult::Done:
                    return RunResult{.code = RunCode::DiagnosticStop, .stage = RunStage::Backend};
                case session::StageResult::Continue:
                    break;
            }
            switch (Terminal::run(exploit_session, input)) {
                case session::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
                case session::StageResult::Done:
                    return RunResult{.code = RunCode::Completed, .stage = RunStage::Terminal};
                case session::StageResult::Continue:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
            }
            return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
        }
    };
} // namespace ghostlock::pipeline

#endif
