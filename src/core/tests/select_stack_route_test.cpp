#include "route/select_stack_route.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include <type_traits>
#include <utility>

using namespace ghostlock;


int32_t main(void) {
    ghostlock::race::PiRace race;
    race.reset(0, 0, 1);
    ghostlock::memory::WriteRequest request{};
    ghostlock::profile::SelectStackLayout layout = {.waiter_shift = 16, .compact_waiter = 1};
    const profile::TargetProfile profile{};

    ghostlock::route::select_stack::SelectStackRoute context(&race, &request, profile, layout, nullptr);
    assert(context.race == &race && context.request == &request);
    assert(&context.profile == &profile);
    assert(context.layout.waiter_shift == 16 && context.layout.compact_waiter);
    assert(!context.pipe_read.valid() && !context.pipe_write.valid());
    assert(!context.block.valid() && !context.high_read.valid());
    assert(!context.stdio_backup[0].valid());
    assert(context.block_borrows_pipe == 0);
    assert(context.status.code == ghostlock::route::ROUTE_RETRYABLE);
    assert(!context.input_set.test(0));
    assert(!context.owned_input_set.test(5));

    /* Borrowed stdio backup is recorded as a borrowed handle. */
    {
        int32_t backups[3] = {100, 101, 102};
        ghostlock::route::select_stack::SelectStackRoute borrowed_stdio(&race, &request, profile, layout, backups);
        assert(borrowed_stdio.stdio_backup[0].get() == 100);
        assert(borrowed_stdio.stdio_backup[2].get() == 102);
    }

    /* route::select_stack::FdSet wrapper: set/test/raw against the syscall layout. */
    {
        route::select_stack::FdSet set;
        set.zero();
        assert(!set.test(7));
        set.set(7);
        assert(set.test(7));
        assert(FD_ISSET(7, set.raw()));
        assert(!set.test(8));
    }

    /* Move-only: no copy, descriptors and owned sets transfer with the move. */
    static_assert(!std::is_copy_constructible_v<ghostlock::route::select_stack::SelectStackRoute>);
    static_assert(!std::is_copy_assignable_v<ghostlock::route::select_stack::SelectStackRoute>);
    static_assert(std::is_move_constructible_v<ghostlock::route::select_stack::SelectStackRoute>);
    {
        ghostlock::route::select_stack::SelectStackRoute source(&race, &request, profile, layout, nullptr);
        int32_t fds[2];
        assert(pipe(fds) == 0);
        source.pipe_read.reset(fds[0]);
        source.pipe_write.reset(fds[1]);
        source.selected_fds_installed = 1;
        source.owned_input_set.set(9);
        ghostlock::route::select_stack::SelectStackRoute moved(std::move(source));
        assert(moved.pipe_read.get() == fds[0]);
        assert(!source.pipe_read.valid());
        assert(moved.pipe_write.get() == fds[1]);
        assert(moved.selected_fds_installed == 1);
        assert(moved.owned_input_set.test(9));
        moved.destroy();
        assert(!moved.pipe_read.valid() && !moved.pipe_write.valid());
        assert(fcntl(fds[0], F_GETFD) == -1 && errno == EBADF);
        assert(fcntl(fds[1], F_GETFD) == -1 && errno == EBADF);
    }

    /* A borrowed block descriptor (timerfd_create fallback) is not closed
   * twice: the pipe read end closes once. */
    {
        ghostlock::route::select_stack::SelectStackRoute borrowed_block(
            &race, &request, profile, layout, nullptr);
        int32_t fds[2];
        assert(pipe(fds) == 0);
        borrowed_block.pipe_read.reset(fds[0]);
        borrowed_block.pipe_write.reset(fds[1]);
        borrowed_block.block_borrows_pipe = 1;
        assert(borrowed_block.block_fd() == fds[0]);
        borrowed_block.destroy();
        assert(fcntl(fds[0], F_GETFD) == -1);
        assert(fcntl(fds[1], F_GETFD) == -1);
    }

    /* A stuck consumer retains every route descriptor for process lifetime. */
    {
        ghostlock::route::select_stack::SelectStackRoute stuck(&race, &request, profile, layout, nullptr);
        int32_t fds[2];
        assert(pipe(fds) == 0);
        stuck.pipe_read.reset(fds[0]);
        stuck.pipe_write.reset(fds[1]);
        stuck.select_errno = 9;
        stuck.consumer_stuck = 1;
        stuck.destroy();
        assert(stuck.status.code == ghostlock::route::ROUTE_DIRTY_FAILURE);
        assert(stuck.status.step == 34);
        assert(stuck.status.error_number == 9);
        assert(!stuck.pipe_read.valid() && !stuck.pipe_write.valid());
        assert(fcntl(fds[0], F_GETFD) != -1);
        assert(fcntl(fds[1], F_GETFD) != -1);
        close(fds[0]);
        close(fds[1]);
    }

    /* stdio restore keeps borrowed descriptors open (dup2 back onto stdout). */
    {
        int32_t saved_stdout = dup(1);
        assert(saved_stdout >= 0);
        int32_t backups[3] = {-1, saved_stdout, -1};
        ghostlock::route::select_stack::SelectStackRoute restored(&race, &request, profile, layout, backups);
        restored.destroy();
        assert(fcntl(saved_stdout, F_GETFD) != -1);
        close(saved_stdout);
    }

    /* disarm is idempotent and marks the route disarmed. */
    context.disarm();
    context.disarm();
    assert(context.status.kernel_disarmed == 1);
    assert(race.consumer_go.load() == 0);

    /* destroy without resources is idempotent and reports a clean fallback. */
    context.destroy();
    context.destroy();
    assert(context.status.userspace_clean == 1);
    assert(context.status.code == ghostlock::route::ROUTE_FALLBACK_SAFE);

    /* fail() records step and errno for the caller's log. */
    assert(context.fail(59, 5) == -1);
    assert(context.status.step == 59);
    assert(context.status.error_number == 5);

    puts("select_stack_route_test: ok");
    return 0;
}
