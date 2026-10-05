#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_TERMINAL_ROOT_CHILD_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_TERMINAL_ROOT_CHILD_HPP

/* ADR-0006 T2/T5 (F5): the root_child terminal procedure belongs to the backend
 * whose chain it finishes. The RootChildPolicy declaration moved here from
 * terminal/root_child.hpp; the neutral terminal layer keeps only the shared
 * pieces (the rooted_child input, script generation, the handoff probe and the
 * UMH command/forward). The namespace follows physical ownership. */

#include "contract/identity.hpp"
#include "contract/stage_result.hpp"
#include "terminal/rooted_child.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::backend::cve_2026_43499::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::contract::ActivationContext;
    using ghostlock::contract::StageResult;
    using ghostlock::terminal::RootedChild;
    /* Batch 4 (D1=B) root_child terminal procedure: the handoff step, moved out
     * of the retired ExploitProcedure so the terminal is a pipeline component.
     * Statement order, ownership and the log text are unchanged. The child
     * lifecycle (victim_context/process) and the KernelSU handoff verification
     * (handoff_probe) stay separate concerns, as noted in contract/identity.hpp. */
    StageResult run_root_child_handoff(CoreSession &session, RootedChild &child);

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
} // namespace ghostlock::backend::cve_2026_43499::terminal

#endif
