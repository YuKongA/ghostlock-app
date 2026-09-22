#ifndef SELECT_STACK_ROUTE_H
#define SELECT_STACK_ROUTE_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "routes/route_status.h"
#include "support/native_resource.hpp"

#include <sys/select.h>

/* Shared with common.h; repeated behind a guard so this header stays
 * host-safe for the fixed tests. */
#ifndef PSELECT_ROUTE_NFDS
#define PSELECT_ROUTE_NFDS 320
#endif

namespace ghostlock {
    /* Bit-access wrapper for one fd_set; syscall boundaries still receive the raw
 * fd_set so the kernel ABI layout stays untouched. */
    class FdSet final {
    public:
        void zero() noexcept { FD_ZERO(&raw_); }
        void set(int fd) noexcept { FD_SET(fd, &raw_); }

        [[nodiscard]] bool test(int fd) const noexcept {
            return FD_ISSET(fd, &raw_) != 0;
        }

        [[nodiscard]] fd_set *raw() noexcept { return &raw_; }
        [[nodiscard]] const fd_set *raw() const noexcept { return &raw_; }

    private:
        fd_set raw_;
    };

    /* Owns every descriptor and fd_set of one select-stack attempt. The stdio
 * backups stay borrowed from the process logging lifetime and are never
 * closed here. A consumer that cannot be drained marks the attempt dirty and
 * retains every route descriptor for process lifetime, instead of letting a
 * destructor close a descriptor a stuck consumer may still walk. */
    class SelectStackRoute final {
    public:
        SelectStackRoute(PiRace *race, const WriteRequest *request,
                         const execution_settings *execution,
                         SelectStackLayout layout,
                         const int stdio_backup[3]) noexcept;

        ~SelectStackRoute() noexcept = default;

        SelectStackRoute(const SelectStackRoute &) = delete;

        SelectStackRoute &operator=(const SelectStackRoute &) = delete;

        SelectStackRoute(SelectStackRoute &&other) noexcept;

        /* Acquire every route descriptor. No consumer trigger is armed until this
   * returns 0; every failure records step/error_number. Defined in
   * route_operations.cpp because it needs the payload page and profile. */
        [[nodiscard]] int prepare() noexcept;

        /* Park the crafted waiter and run select/pselect. Defined in
   * route_operations.cpp. */
        [[nodiscard]] RouteStatus execute() noexcept;

        /* Stop the consumer trigger and bounded-drain it; a consumer still in
   * flight marks the route dirty. */
        void disarm() noexcept;

        /* Restore stdio, close every owned descriptor once, or retain everything
   * when the consumer was stuck. */
        void destroy() noexcept;

        [[nodiscard]] int fail(int step, int error_number) noexcept;

        /* The route may borrow the pipe read end when timerfd_create fails. */
        [[nodiscard]] int block_fd() const noexcept {
            return block_borrows_pipe ? pipe_read.get() : block.get();
        }

        PiRace *race = nullptr;
        const WriteRequest *request = nullptr;
        const struct execution_settings *execution = nullptr;
        SelectStackLayout layout{};
        FdSet input_set;
        FdSet output_set;
        FdSet exception_set;
        FdSet owned_input_set;
        FdSet owned_output_set;
        FdSet owned_exception_set;
        UniqueFd pipe_read;
        UniqueFd pipe_write;
        UniqueFd block;
        UniqueFd high_read;
        BorrowedFd stdio_backup[3];
        int block_borrows_pipe = 0;
        int selected_fds_installed = 0;
        int consumer_stuck = 0;
        int calls = 0;
        int successes = 0;
        int select_result = 0;
        int select_errno = 0;
        RouteStatus status{};

    private:
        void retain_for_process_lifetime() noexcept;
    };
    using SelectStackRouteContext = SelectStackRoute;
} // namespace ghostlock

#endif
