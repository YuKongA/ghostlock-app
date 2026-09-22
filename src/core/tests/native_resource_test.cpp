#include "support/native_resource.hpp"

#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>

using namespace ghostlock;

namespace {
    void *increment(void *argument) {
        static_cast<std::atomic<int> *>(argument)->fetch_add(1);
        return nullptr;
    }

    struct StopState {
        std::atomic<bool> stop{false};
        std::atomic<int> stop_calls{0};
    };

    void request_stop(void *argument) noexcept {
        auto *state = static_cast<StopState *>(argument);
        state->stop_calls.fetch_add(1);
        state->stop.store(true);
    }

    void *wait_for_stop(void *argument) {
        auto *state = static_cast<StopState *>(argument);
        while (!state->stop.load()) sched_yield();
        return nullptr;
    }

    int open_fd_count() {
        DIR *directory = opendir("/proc/self/fd");
        if (!directory) directory = opendir("/dev/fd");
        assert(directory);
        int count = 0;
        while (readdir(directory)) ++count;
        closedir(directory);
        return count;
    }
} // namespace

int main() {
    const int initial_fd_count = open_fd_count();
    int pipe_fd[2];
    assert(pipe(pipe_fd) == 0);
    close(pipe_fd[1]);
    const int observed = pipe_fd[0];
    {
        UniqueFd first(observed);
        UniqueFd second(std::move(first));
        assert(!first.valid() && second.get() == observed);
        BorrowedFd borrowed = second.borrow();
        assert(borrowed.valid() && borrowed.get() == observed);
    }
    errno = 0;
    assert(fcntl(observed, F_GETFD) == -1 && errno == EBADF);
    assert(open_fd_count() == initial_fd_count);

    int scope_calls = 0;
    {
        auto first = make_scope_exit([&]() noexcept { ++scope_calls; });
        auto second = std::move(first);
        (void) second;
    }
    assert(scope_calls == 1);
    {
        auto cancelled =
                make_scope_exit([&]() noexcept { ++scope_calls; });
        cancelled.release();
    }
    assert(scope_calls == 1);

    auto mapping = MappedRegion::map_anonymous(
        4096, PROT_READ | PROT_WRITE);
    assert(mapping && mapping.value().bytes().size() == 4096);
    mapping.value().bytes()[0] = std::byte{0x5a};
    MappedRegion moved_mapping(std::move(mapping.value()));
    assert(moved_mapping.valid() && !mapping.value().valid());
    auto invalid_mapping =
            MappedRegion::map_anonymous(0, PROT_READ | PROT_WRITE);
    assert(!invalid_mapping);

    std::atomic<int> calls{0};
    PthreadOwner worker;
    assert(worker.start(increment, &calls) == 0);
    assert(worker.join() == 0 && calls.load() == 1);

    StopState stop_state;
    PthreadOwner stoppable;
    assert(stoppable.start(wait_for_stop, &stop_state, request_stop,
                           &stop_state) == 0);
    stoppable.request_stop();
    stoppable.request_stop();
    assert(stoppable.state() ==
           PthreadOwner::State::StopRequested);
    assert(stoppable.join() == 0);
    assert(stop_state.stop_calls.load() == 1);
    assert(stoppable.state() == PthreadOwner::State::Joined);

    PthreadOwner invalid_worker;
    assert(invalid_worker.start(nullptr, nullptr) == EINVAL);

    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        for (;;) pause();
    }
    ChildProcess child(pid);
    assert(child.terminate_and_wait(SIGKILL) == 0);
    assert(!child.valid());
    assert(child.state() == ChildProcess::State::Reaped);
    assert(child.terminate_and_wait(SIGKILL) == EINVAL);

    const pid_t handoff_pid = fork();
    assert(handoff_pid >= 0);
    if (handoff_pid == 0) {
        for (;;) pause();
    }
    ChildProcess handoff(handoff_pid);
    assert(handoff.release_to_handoff() == handoff_pid);
    assert(handoff.state() == ChildProcess::State::Transferred);
    assert(kill(handoff_pid, SIGKILL) == 0);
    assert(waitpid(handoff_pid, nullptr, 0) == handoff_pid);

    ChildProcess invalid_child(-1);
    assert(!invalid_child.valid());
    assert(invalid_child.release_to_handoff() == -1);

    /* An externally reaped child is marked so the owner never waits again. */
    const pid_t reaped_pid = fork();
    assert(reaped_pid >= 0);
    if (reaped_pid == 0) _exit(0);
    ChildProcess reaped(reaped_pid);
    assert(waitpid(reaped_pid, nullptr, 0) == reaped_pid);
    reaped.mark_reaped();
    assert(!reaped.valid());
    assert(reaped.state() == ChildProcess::State::Reaped);
    reaped.mark_reaped(); /* idempotent */
    assert(reaped.terminate_and_wait(SIGKILL) == EINVAL);

    /* fork() failure injection: a failed fork must leave no owned child. The
   * limit is best-effort, so a platform that still forks is also acceptable
   * and the child is reaped. */
    struct rlimit saved_limit{};
    if (getrlimit(RLIMIT_NPROC, &saved_limit) == 0) {
        struct rlimit exhausted = saved_limit;
        exhausted.rlim_cur = 0;
        if (setrlimit(RLIMIT_NPROC, &exhausted) == 0) {
            errno = 0;
            const pid_t failed = fork();
            if (failed == 0) _exit(0);
            if (failed > 0) {
                kill(failed, SIGKILL);
                waitpid(failed, nullptr, 0);
            } else {
                assert(errno == EAGAIN || errno == ENOMEM);
                ChildProcess none(failed);
                assert(!none.valid());
                assert(none.state() == ChildProcess::State::Empty);
                assert(none.terminate_and_wait(SIGKILL) == EINVAL);
            }
            (void) setrlimit(RLIMIT_NPROC, &saved_limit);
        }
    }
    return 0;
}
