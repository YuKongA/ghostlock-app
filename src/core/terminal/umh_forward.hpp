#ifndef GHOSTLOCK_TERMINAL_UMH_FORWARD_HPP
#define GHOSTLOCK_TERMINAL_UMH_FORWARD_HPP

/* umh_forward terminal execution policy (B5-8).
 *
 * The cve_2026_43284 backend fills terminal::UmhForwardInput only after
 * run_backend_terminal observed the LKM/UMH terminus (lkm_loaded) and the chain
 * release ran. This terminal consumes that handoff: it validates the terminus,
 * forwards the backend-built command to the injected kernel UMH channel and
 * waits for the App-selected root program to become ready.
 *
 * The forward/wait surface is a neutral, injected handle carried in the input
 * (terminal/terminal_input.hpp::UmhForwardChannel); the real kernel
 * call_usermodehelper/device binding is B5-9. Every check is fail-closed: a
 * missing channel, a missing terminus or a non-Ready forward can never be
 * treated as success and never degrades to root_child.
 *
 * Availability is owned by component_catalog::terminal_available(); this type
 * carries only the stable id, the input/activation and the step. The terminal
 * stays unavailable (not device-verified) until the B5-9 gate, so wiring it into
 * the catalogue never makes it runnable on a device. */

#include "pipeline/component_catalog.hpp"
#include "pipeline/terminal_contract.hpp"
#include "session/stage_types.hpp"
#include "terminal/terminal_input.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;

    /* Terminal step: validate the backend handoff and forward/wait. Session is
     * unused today (the captureless policy has no per-run profile); it stays in
     * the signature so the unified terminal contract is fixed. */
    [[nodiscard]] StageResult run_umh_forward(UmhForwardInput &input) noexcept;

    struct UmhForwardPolicy final {
        static constexpr pipeline::TerminalKind kind = pipeline::TerminalKind::UmhForward;
        using Input = UmhForwardInput;
        static constexpr ActivationContext activation = ActivationContext::KernelSpawned;

        [[nodiscard]] static StageResult run(CoreSession &session, Input &input);
    };

    static_assert(UmhForwardPolicy::kind == pipeline::terminal::UmhForwardTerminal::kind);
    static_assert(pipeline::TerminalIdentity<UmhForwardPolicy>);
    static_assert(pipeline::TerminalExecution<UmhForwardPolicy>);
} // namespace ghostlock::terminal

#endif
