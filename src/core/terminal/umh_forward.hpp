#ifndef GHOSTLOCK_TERMINAL_UMH_FORWARD_HPP
#define GHOSTLOCK_TERMINAL_UMH_FORWARD_HPP

/* umh_forward terminal execution policy (B5-8 / B6-T5).
 *
 * Read-only readiness terminal. The cve_2026_43284 backend fills
 * terminal::UmhForwardInput only after run_backend_terminal observed the
 * LKM/UMH terminus (lkm_loaded) and the chain release ran. The endgame chain
 * itself -- our own LKM plus the kernel-side call_usermodehelper it performs --
 * completes the module load; this terminal writes nothing, forwards nothing and
 * execs nothing. It only reads back the device facts the readiness probe
 * exposes (/dev/dfm0 present and /proc/modules contains kernelsu) and reports
 * Done on Ready, Failed on NotReady/Unavailable.
 *
 * session_secrets are injected by the composition root and consumed by the
 * chain; the terminal never reads, forwards or persists them.
 *
 * The readiness surface is a neutral, injected handle carried in the input
 * (terminal/terminal_input.hpp::UmhForwardChannel). Every check is fail-closed:
 * a missing terminus or an unbound/non-Ready probe can never be treated as
 * success and never degrades to root_child.
 *
 * Availability is owned by contract::terminal_available(); this type carries
 * only the stable id, the input/activation and the step. The B6/T5 production
 * seam is wired but the terminal stays unavailable (not device-verified) until
 * the main agent flips availability after the app-call device gate; see
 * contract/identity.hpp for the exact flip location. */

#include "contract/identity.hpp"
#include "contract/stage_result.hpp"
#include "terminal/terminal_input.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::contract::StageResult;

    /* Terminal step: validate the backend handoff and read-only confirm
     * readiness. Session is unused today (the captureless policy has no
     * per-run profile); it stays in the signature so the unified terminal
     * contract is fixed. */
    [[nodiscard]] StageResult run_umh_forward(UmhForwardInput &input) noexcept;

    /* Production read-only readiness probe. On Linux: Ready when /dev/dfm0
     * exists and /proc/modules contains kernelsu; NotReady otherwise;
     * Unavailable when the probe cannot read. On a non-Linux host the channel
     * is left unbound (invalid), so the terminal fails closed. Performs no
     * write, forward or exec. */
    [[nodiscard]] UmhForwardChannel production_umh_channel() noexcept;

    struct UmhForwardPolicy final {
        static constexpr contract::TerminalKind kind = contract::TerminalKind::UmhForward;
        using Input = UmhForwardInput;
        static constexpr ActivationContext activation = ActivationContext::KernelSpawned;

        [[nodiscard]] static StageResult run(CoreSession &session, Input &input);
    };

    static_assert(UmhForwardPolicy::kind == contract::terminal::UmhForwardTerminal::kind);
    static_assert(contract::TerminalIdentity<UmhForwardPolicy>);
    static_assert(contract::TerminalExecution<UmhForwardPolicy>);
} // namespace ghostlock::terminal

#endif
