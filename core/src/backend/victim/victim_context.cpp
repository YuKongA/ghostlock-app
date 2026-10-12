/** @file backend/victim/victim_context.cpp
 *  The victim context's two explicit ends: retire (kill + reap) and close.
 */
#include "backend/victim/victim_context.hpp"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace ghostlock::backend::victim {
    namespace {
        void close_one(std::int32_t &fd) noexcept {
            if (fd >= 0) {
                static_cast<void>(::close(fd));
                fd = -1;
            }
        }
    }  // namespace

    void VictimContext::retire_child() noexcept {
        if (child_ <= 0) {
            return;
        }
        /* 进程组一起收：退役的 kill(-pid) 是为了带上子进程自己 fork 的东西 ✓。 */
        static_cast<void>(::kill(-child_, SIGKILL));
        static_cast<void>(::kill(child_, SIGKILL));
        while (::waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {
        }
        child_ = -1;
    }

    void VictimContext::close_ends() noexcept {
        close_one(task_read);
        close_one(task_write);
        close_one(cmd_read);
        close_one(cmd_write);
        close_one(uid_read);
        close_one(uid_write);
    }
}  // namespace ghostlock::backend::victim
