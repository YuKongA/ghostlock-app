#ifndef GHOSTLOCK_TERMINAL_ROOTED_CHILD_HPP
#define GHOSTLOCK_TERMINAL_ROOTED_CHILD_HPP

#include "support/native_resource.hpp"
#include "terminal/terminal_input.hpp"

#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ghostlock::terminal {
    /* Neutral, move-only handle to one rooted child handed from a backend to a
 * terminal action (ADR-0004 R10). The backend transfers ownership at the
 * terminal boundary (VictimContext::release_child + the parked command fd);
 * scope exit closes the command fd but never signals the child -- retire() is
 * the only kill path, mirroring VictimContext's explicit-retirement contract. */
    struct RootedChild final : TerminalInput {
        pid_t pid = -1;
        ghostlock::support::UniqueFd command{};
        /* Victim-context read end handed over so the terminal resets it at the
         * original point (keeps the pre-transfer cleanup order). */
        ghostlock::support::UniqueFd uid_read{};
        bool alive = false;
        bool seccomp_bypassed = false;
        bool ever_rooted = false;

        RootedChild() noexcept = default;

        ~RootedChild() noexcept = default;

        RootedChild(const RootedChild &) = delete;

        RootedChild &operator=(const RootedChild &) = delete;

        [[nodiscard]] bool valid() const noexcept { return pid > 0; }

        /* Explicit SIGKILL + reap for a child this handle still owns. */
        void retire() noexcept {
            if (pid > 0) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, nullptr, 0);
            }
            pid = -1;
            command.reset();
            uid_read.reset();
        }

        /* Transfer the pid away (the child outlives this handle); drop the fds. */
        [[nodiscard]] pid_t detach() noexcept {
            const pid_t detached = pid;
            pid = -1;
            command.reset();
            uid_read.reset();
            return detached;
        }
    };
} // namespace ghostlock::terminal

#endif
