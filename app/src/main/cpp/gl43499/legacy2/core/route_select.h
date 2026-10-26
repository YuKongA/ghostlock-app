/**
 * @file route_select.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_ROUTE_SELECT_H
#define GHOSTLOCK_CORE_ROUTE_SELECT_H

#include "route.h"
#include "race.h"
#include "memory.h"
#include "support.h"
#include "profile.h"
#include "params.h"

/* ==== route/select_stack_route.h ==== */
#ifndef SELECT_STACK_ROUTE_H
#define SELECT_STACK_ROUTE_H


#include <sys/select.h>

#include <array>

/* Shared with common.h; repeated behind a guard so this header stays
 * host-safe for the fixed tests. */
#ifndef PSELECT_ROUTE_NFDS
#define PSELECT_ROUTE_NFDS 320
#endif

namespace ghostlock::route::select_stack {
    /* Bit-access wrapper for one fd_set; syscall boundaries still receive the raw
 * fd_set so the kernel ABI layout stays untouched. */
    class FdSet final {
    public:
        void zero() noexcept { FD_ZERO(&raw_); }
        void set(int32_t fd) noexcept { FD_SET(fd, &raw_); }

        [[nodiscard]] bool test(int32_t fd) const noexcept {
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
        explicit SelectStackRoute(const ghostlock::core::SelectInputs &inputs) noexcept;

        ~SelectStackRoute() noexcept = default;

        SelectStackRoute(const SelectStackRoute &) = delete;

        SelectStackRoute &operator=(const SelectStackRoute &) = delete;

        SelectStackRoute(SelectStackRoute &&other) noexcept;

        /* Acquire every route descriptor. No consumer trigger is armed until this
   * returns 0; every failure records step/error_number. Defined in the
   * route-level part of select_stack_route.cpp; it needs the payload page and profile. */
        [[nodiscard]] int32_t prepare() noexcept;

        /* Park the crafted waiter and run select/pselect. Defined in the
   * route-level part of select_stack_route.cpp. */
        [[nodiscard]] ghostlock::route::RouteStatus execute() noexcept;

        /* Stop the consumer trigger and bounded-drain it; a consumer still in
   * flight marks the route dirty. */
        void disarm() noexcept;

        /* Restore stdio, close every owned descriptor once, or retain everything
   * when the consumer was stuck. */
        void destroy() noexcept;

        [[nodiscard]] int32_t fail(int32_t step, int32_t error_number) noexcept;

        /* The route may borrow the pipe read end when timerfd_create fails. */
        [[nodiscard]] int32_t block_fd() const noexcept {
            return block_borrows_pipe ? pipe_read.get() : block.get();
        }

        ghostlock::core::SelectInputs in;
        FdSet input_set;
        FdSet output_set;
        FdSet exception_set;
        FdSet owned_input_set;
        FdSet owned_output_set;
        FdSet owned_exception_set;
        ghostlock::support::UniqueFd pipe_read;
        ghostlock::support::UniqueFd pipe_write;
        ghostlock::support::UniqueFd block;
        ghostlock::support::UniqueFd high_read;
        int32_t block_borrows_pipe = 0;
        int32_t selected_fds_installed = 0;
        int32_t consumer_stuck = 0;
        int32_t calls = 0;
        int32_t successes = 0;
        int32_t select_result = 0;
        int32_t select_errno = 0;
        ghostlock::route::RouteStatus status{};

    private:
        void retain_for_process_lifetime() noexcept;
    };
} // namespace ghostlock::route::select_stack

#endif

#endif // GHOSTLOCK_CORE_ROUTE_SELECT_H
