#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "contract/model.hpp"
#include "session/core_session.hpp"
#include "contract/stage_result.hpp"
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
                if constexpr (contract::BackendState<Backend>) {
                    Backend::state_construct(session);
                }
            }

            BackendStateGuard(const BackendStateGuard &) = delete;

            BackendStateGuard &operator=(const BackendStateGuard &) = delete;

            ~BackendStateGuard() noexcept {
                if constexpr (contract::BackendState<Backend>) {
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
        static constexpr contract::BackendKind backend = Backend::kind;
        static constexpr contract::StepSetKind steps = Backend::steps;
        static constexpr contract::TerminalKind terminal = Terminal::kind;
        static constexpr DispatchTarget target = dispatch_target_of(backend, steps, terminal);
        static_assert(target != DispatchTarget::None,
                      "pipeline must be a catalogued (backend, steps, terminal) triple");
        static_assert(contract::TerminalExecution<Terminal>,
                      "terminal must satisfy the terminal execution contract");
        static_assert(contract::BackendExecution<Backend, typename Terminal::Input>,
                      "backend must satisfy the execution contract for the terminal input");

        [[nodiscard]] static RunResult run(session::CoreSession &exploit_session,
                                           const profile::Document &document,
                                           const char *debug_dir, bool force_attack) {
            const detail::BackendStateGuard<Backend> state_guard(exploit_session);
            /* Bind/install the backend profile outside the PI window; a
             * rejected document never reaches the attack. */
            if (!Backend::state_from(exploit_session, document).ok()) {
                return RunResult{.code = RunCode::Rejected, .stage = RunStage::Backend};
            }
            typename Terminal::Input input{};
            switch (Backend::run(exploit_session, debug_dir, force_attack, input)) {
                case contract::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Backend};
                case contract::StageResult::Done:
                    return RunResult{.code = RunCode::DiagnosticStop, .stage = RunStage::Backend};
                case contract::StageResult::Continue:
                    break;
            }
            switch (Terminal::run(exploit_session, input)) {
                case contract::StageResult::Failed:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
                case contract::StageResult::Done:
                    return RunResult{.code = RunCode::Completed, .stage = RunStage::Terminal};
                case contract::StageResult::Continue:
                    return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
            }
            return RunResult{.code = RunCode::Failed, .stage = RunStage::Terminal};
        }
    };
} // namespace ghostlock::pipeline

#endif
