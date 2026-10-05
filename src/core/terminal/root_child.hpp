#ifndef GHOSTLOCK_TERMINAL_ROOT_CHILD_HPP
#define GHOSTLOCK_TERMINAL_ROOT_CHILD_HPP

#include "contract/identity.hpp"
#include "contract/stage_result.hpp"
#include "terminal/rooted_child.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::contract::StageResult;
    /* Batch 4 (D1=B) root_child terminal procedure: the handoff step, moved out
     * of the retired ExploitProcedure so the terminal is a pipeline component.
     * Statement order, ownership and the log text are unchanged. The child
     * lifecycle (victim_context/process) and the KernelSU handoff verification
     * (handoff_probe) stay separate concerns, as noted in contract/identity.hpp. */
    StageResult run_root_child_handoff(CoreSession &session, ghostlock::terminal::RootedChild &child);

    /* Availability is owned by contract::terminal_available(); the
     * execution policies carry only the id and, when available, the step. */
    struct RootChildPolicy final {
        static constexpr contract::TerminalKind kind = contract::TerminalKind::RootChild;
        using Input = RootedChild;
        static constexpr ActivationContext activation = ActivationContext::Descendant;

        /* Terminal step: settle, root-shell handoff and KernelSU late-load. */
        [[nodiscard]] static StageResult run(CoreSession &session, Input &child);
    };

    static_assert(RootChildPolicy::kind == contract::terminal::RootChildTerminal::kind);
    static_assert(contract::TerminalIdentity<RootChildPolicy>);
    static_assert(contract::TerminalExecution<RootChildPolicy>);
} // namespace ghostlock::terminal

#endif
