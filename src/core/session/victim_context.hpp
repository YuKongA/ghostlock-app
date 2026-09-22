#ifndef GHOSTLOCK_VICTIM_CONTEXT_HPP
#define GHOSTLOCK_VICTIM_CONTEXT_HPP

#include "support/native_resource.hpp"

#include <sys/types.h>

#include <utility>

namespace ghostlock {
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

        UniqueFd task_read;
        UniqueFd cmd_write;
        UniqueFd uid_read;
        UniqueFd task_write;
        UniqueFd cmd_read;
        UniqueFd uid_write;

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
} // namespace ghostlock

#endif
