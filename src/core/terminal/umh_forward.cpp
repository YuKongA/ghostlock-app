/* umh_forward terminal execution policy (B5-8) -- implementation.
 *
 * Pure and host-testable: it touches no device, forks nothing and writes no
 * file. The forward/wait is the injected UmhForwardChannel handle, so the host
 * test drives every path with fakes; the kernel binding lands in B5-9. */

#include "terminal/umh_forward.hpp"

namespace ghostlock::terminal {

    StageResult run_umh_forward(UmhForwardInput &input) noexcept {
        /* The backend fills the input only after a clean LKM/UMH terminus; if any
         * of these is absent the handoff is incomplete and we fail closed rather
         * than launch anything. */
        if (!input.lkm_loaded) {
            return StageResult::Failed;
        }
        if (input.command.argc == 0U) {
            return StageResult::Failed;
        }
        if (input.root_program.argv_view().empty()) {
            return StageResult::Failed;
        }
        if (input.session_secrets == nullptr || input.session_secrets_size == 0U) {
            return StageResult::Failed;
        }
        if (!input.channel.valid()) {
            return StageResult::Failed;
        }

        const UmhForwardOutcome outcome =
                input.channel.forward(input.channel.ctx, input.root_program,
                                      input.command, input.channel.wait_timeout_ms);
        if (outcome != UmhForwardOutcome::Ready) {
            return StageResult::Failed;
        }

        /* KernelSU readiness is a device fact (handoff_probe in the real build).
         * Only a ksud-style selection needs it, and an unbound probe fails closed
         * instead of being assumed ready. Other programs report success once the
         * channel forwarded them. */
        if (input.root_program.kind == RootProgramKind::KernelSU) {
            if (input.channel.ready == nullptr) {
                return StageResult::Failed;
            }
            if (input.channel.ready(input.channel.ready_ctx) != UmhReadyState::Ready) {
                return StageResult::Failed;
            }
        }
        return StageResult::Done;
    }

    StageResult UmhForwardPolicy::run(CoreSession &session, Input &input) {
        (void)session;
        return run_umh_forward(input);
    }

} // namespace ghostlock::terminal
