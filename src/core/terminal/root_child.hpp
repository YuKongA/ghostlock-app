#ifndef GHOSTLOCK_TERMINAL_ROOT_CHILD_HPP
#define GHOSTLOCK_TERMINAL_ROOT_CHILD_HPP

#include "pipeline/component_catalog.hpp"
#include "pipeline/terminal_contract.hpp"
#include "session/stage_types.hpp"
#include "terminal/rooted_child.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;
    /* Batch 4 (D1=B) root_child terminal procedure: the handoff step, moved out
     * of the retired ExploitProcedure so the terminal is a pipeline component.
     * Statement order, ownership and the log text are unchanged. The child
     * lifecycle (victim_context/process) and the KernelSU handoff verification
     * (handoff_probe) stay separate concerns, as noted in terminal_contract. */
    StageResult run_root_child_handoff(CoreSession &session, ghostlock::terminal::RootedChild &child);

    /* Availability is owned by component_catalog::terminal_available(); the
     * execution policies carry only the id and, when available, the step. */
    struct RootChildPolicy final {
        static constexpr pipeline::TerminalKind kind = pipeline::TerminalKind::RootChild;

        /* Terminal step: settle, root-shell handoff and KernelSU late-load. */
        [[nodiscard]] static StageResult run(CoreSession &session, ghostlock::terminal::RootedChild &child);
    };

    struct UmhForwardPolicy final {
        static constexpr pipeline::TerminalKind kind = pipeline::TerminalKind::UmhForward;
    };

    static_assert(RootChildPolicy::kind == pipeline::terminal::RootChildTerminal::kind);
    static_assert(UmhForwardPolicy::kind == pipeline::terminal::UmhForwardTerminal::kind);
    static_assert(pipeline::TerminalIdentity<RootChildPolicy>);
    static_assert(pipeline::TerminalExecution<RootChildPolicy>);
    static_assert(pipeline::TerminalIdentity<UmhForwardPolicy>);
    static_assert(!pipeline::TerminalExecution<UmhForwardPolicy>);
} // namespace ghostlock::terminal

#endif
