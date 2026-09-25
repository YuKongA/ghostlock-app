#ifndef GHOSTLOCK_ROOT_CHILD_FRONTEND_HPP
#define GHOSTLOCK_ROOT_CHILD_FRONTEND_HPP

#include "route/component_catalog.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::session::frontend {
    /* Batch 4 (D1=B) root_child frontend procedure: the handoff step, moved out
     * of the retired ExploitProcedure so the frontend is a pipeline component.
     * Statement order, ownership and the log text are unchanged. The child
     * lifecycle (victim_context/process) and the KernelSU handoff verification
     * (handoff_probe) stay separate concerns, as noted in frontend_contract. */
    StageResult run_root_child_handoff(ExploitSession &session, const VictimChain &chain);

    struct RootChildPolicy final {
        static constexpr runtime::FrontendKind kind = runtime::FrontendKind::RootChild;
        static constexpr bool available = true;

        /* Frontend step: settle, root-shell handoff and KernelSU late-load. */
        [[nodiscard]] static StageResult run(ExploitSession &session, const VictimChain &chain);
    };

    struct UmhForwardPolicy final {
        static constexpr runtime::FrontendKind kind = runtime::FrontendKind::UmhForward;
        static constexpr bool available = false;
    };
} // namespace ghostlock::session::frontend

#endif
