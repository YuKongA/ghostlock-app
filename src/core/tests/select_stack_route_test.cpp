#include "route/select_stack_route.h"

#include <array>
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

#include <type_traits>
#include <utility>

using namespace ghostlock;


int32_t main(void) {
    ghostlock::race::PiRace race;
    assert(race.reset(0, 0, 1));
    ghostlock::memory::WriteRequest request{};
    ghostlock::profile::SelectStackLayout layout = {.waiter_shift = 16, .compact_waiter = 1};
    const profile::TargetProfile profile{};
    const std::array<int32_t, 3> no_stdio = {-1, -1, -1};

    ghostlock::route::select_stack::SelectStackRoute context(&race, &request, profile, layout, no_stdio);
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
        const std::array<int32_t, 3> backups = {100, 101, 102};
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
        ghostlock::route::select_stack::SelectStackRoute source(&race, &request, profile, layout, no_stdio);
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
            &race, &request, profile, layout, no_stdio);
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
        ghostlock::route::select_stack::SelectStackRoute stuck(&race, &request, profile, layout, no_stdio);
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
        const std::array<int32_t, 3> backups = {-1, saved_stdout, -1};
        ghostlock::route::select_stack::SelectStackRoute restored(&race, &request, profile, layout, backups);
        restored.destroy();
        assert(fcntl(saved_stdout, F_GETFD) != -1);
        close(saved_stdout);
    }

    /* The pass must snapshot every slot it rebinds -- open, closed, and the
     * forced one, whose bit the stamp may leave clear -- and hand them back. */
    {
        /* Park every pipe end above the route's range, so the low slots below
         * are free and each pipe keeps its identity through the pass. */
        int32_t routed[2] = {-1, -1}, plain[2] = {-1, -1}, cloexec[2] = {-1, -1};
        assert(pipe(routed) == 0 && pipe(plain) == 0 && pipe(cloexec) == 0);
        int32_t *ends[3] = {routed, plain, cloexec};
        for (int32_t *pair : ends) {
            for (int32_t end = 0; end < 2; end++) {
                const int32_t high =
                    fcntl(pair[end], F_DUPFD_CLOEXEC, PSELECT_ROUTE_NFDS + 256);
                assert(high >= 0);
                close(pair[end]);
                pair[end] = high;
            }
        }

        const int32_t open_slot = 7;   /* open, without FD_CLOEXEC */
        const int32_t closed_slot = 9; /* closed when the pass runs */
        const int32_t forced_slot = static_cast<int32_t>(PSELECT_ROUTE_NFDS) - 1;
        assert(dup2(plain[0], open_slot) == open_slot);
        assert(dup2(cloexec[0], forced_slot) == forced_slot);
        assert(fcntl(forced_slot, F_SETFD, FD_CLOEXEC) == 0);
        (void) close(closed_slot);

        fd_set in, out, ex;
        FD_ZERO(&in);
        FD_ZERO(&out);
        FD_ZERO(&ex);
        FD_SET(open_slot, &in);
        FD_SET(closed_slot, &ex);
        /* forced_slot is left clear: the pass installs it and must snapshot it
         * all the same. */

        int32_t error_number = -1;
        const int32_t unbacked =
            ghostlock::route::open_selected_fds(&in, &out, &ex, routed[0], -1,
                                                &error_number);
        assert(unbacked == 0);      /* every slot it took was copied first */
        assert(error_number == 0);

        struct stat taken{}, app{};
        assert(fstat(routed[0], &app) == 0);
        assert(fstat(open_slot, &taken) == 0 && taken.st_ino == app.st_ino);
        assert(fstat(closed_slot, &taken) == 0 && taken.st_ino == app.st_ino);
        assert(fstat(forced_slot, &taken) == 0 && taken.st_ino == app.st_ino);
        assert(FD_ISSET(forced_slot, &ex));

        ghostlock::route::restore_selected_fds();

        assert(fstat(plain[0], &app) == 0);
        assert(fstat(open_slot, &taken) == 0 && taken.st_ino == app.st_ino);
        assert((fcntl(open_slot, F_GETFD) & FD_CLOEXEC) == 0);
        assert(fstat(cloexec[0], &app) == 0);
        assert(fstat(forced_slot, &taken) == 0 && taken.st_ino == app.st_ino);
        assert((fcntl(forced_slot, F_GETFD) & FD_CLOEXEC) != 0);
        errno = 0;
        assert(fcntl(closed_slot, F_GETFD) == -1 && errno == EBADF);

        for (int32_t *pair : ends) {
            close(pair[0]);
            close(pair[1]);
        }
        close(open_slot);
        close(forced_slot);
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
