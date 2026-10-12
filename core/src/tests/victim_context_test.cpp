/** @file tests/victim_context_test.cpp
 *  受害者上下文的判据：六个端的状态、显式交接、显式退场（kill + 回收）。
 *  能失败的那一面：交接后 pid 必须不在本上下文、close 必须幂等、retire 必须真的回收。
 */
#include <cstdio>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "backend/victim/victim_context.hpp"

namespace {
    using ghostlock::backend::victim::VictimContext;

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) {
            std::printf("victim context: FAIL %s\n", what);
            ++g_failures;
        }
    }

    void a_fresh_context_owns_nothing() {
        VictimContext victim{};
        expect(!victim.ends_open(), "fresh context has no ends");
        expect(victim.child() == -1, "fresh context has no child");
    }

    void ends_open_and_close_idempotently() {
        VictimContext victim{};
        int task[2] = {-1, -1};
        int cmd[2] = {-1, -1};
        int uid[2] = {-1, -1};
        expect(::pipe(task) == 0 && ::pipe(cmd) == 0 && ::pipe(uid) == 0, "pipes open");
        victim.task_read = task[0];
        victim.task_write = task[1];
        victim.cmd_read = cmd[0];
        victim.cmd_write = cmd[1];
        victim.uid_read = uid[0];
        victim.uid_write = uid[1];
        expect(victim.ends_open(), "ends open");
        victim.close_ends();
        expect(!victim.ends_open(), "ends closed");
        victim.close_ends();
        expect(!victim.ends_open(), "close is idempotent");
    }

    void release_and_mark_take_the_pid_away() {
        VictimContext victim{};
        victim.set_child(4242);
        expect(victim.release_child() == 4242, "release hands the pid over");
        expect(victim.child() == -1, "and the context no longer owns it");
        victim.set_child(4243);
        victim.mark_child_exited();
        expect(victim.child() == -1, "mark drops the pid without signalling");
    }

    void retire_kills_and_reaps() {
        VictimContext victim{};
        const pid_t child = ::fork();
        expect(child >= 0, "fork");
        if (child == 0) {
            for (;;) { ::pause(); }
        }
        victim.set_child(child);
        victim.retire_child();
        expect(victim.child() == -1, "retire drops the pid");
        /* 已被回收：再 waitpid 应当说 ECHILD。 */
        expect(::waitpid(child, nullptr, WNOHANG) == -1, "the child was reaped");
    }
}  // namespace

int main() {
    std::printf("victim context: the explicit ends");
    a_fresh_context_owns_nothing();
    ends_open_and_close_idempotently();
    release_and_mark_take_the_pid_away();
    retire_kills_and_reaps();
    if (g_failures != 0U) {
        std::printf("victim context: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("victim context: PASS (ownership is explicit, close is idempotent, retire reaps)\n");
    return 0;
}
