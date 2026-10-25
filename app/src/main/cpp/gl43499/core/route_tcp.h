/**
 * @file route_tcp.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_ROUTE_TCP_H
#define GHOSTLOCK_CORE_ROUTE_TCP_H

#include "route.h"
#include "race.h"
#include "memory.h"
#include "support.h"
#include "profile.h"
#include "params.h"

/* ==== route/tcp_zerocopy_route.h ==== */
#ifndef TCP_ZEROCOPY_ROUTE_H
#define TCP_ZEROCOPY_ROUTE_H


#include <pthread.h>
#include <atomic>
#include <cstddef>

namespace ghostlock::route::tcp_zerocopy {
    /* Owns every userspace resource of one TCP zerocopy attempt: the loopback
 * pair, the punching memfd and its shared mapping, and the punch worker.
 * Lifetime is explicit: prepare() -> execute() -> disarm() -> destroy().
 * Driver fields stay public because the route-level driver in this file runs the trigger
 * directly from the hot loop and a punch worker trampoline reads the atomics.
 * A dirty failure deliberately retains the resources for process lifetime so
 * the still-running puncher never sees a recycled descriptor. */
    class TcpZerocopyRoute final {
    public:
        explicit TcpZerocopyRoute(const ghostlock::core::TcpInputs &inputs) noexcept;

        ~TcpZerocopyRoute() noexcept = default;

        TcpZerocopyRoute(const TcpZerocopyRoute &) = delete;

        TcpZerocopyRoute &operator=(const TcpZerocopyRoute &) = delete;

        TcpZerocopyRoute(TcpZerocopyRoute &&) = delete;

        /* Acquire every userspace resource. No PI consumer or punch operation is
   * armed until this returns 0; every failure records step/error_number. */
        [[nodiscard]] int32_t prepare() noexcept;

        /* Run the trigger after prepare() established exclusive ownership. */
        [[nodiscard]] ghostlock::route::RouteStatus execute() noexcept;

        /* Stop every trigger and drain the consumer. */
        void disarm() noexcept;

        /* Join the worker, then release each owned resource once. Join or munmap
   * failures mark the attempt dirty and retain the remaining resources for
   * process lifetime, matching the C implementation. */
        void destroy() noexcept;

        [[nodiscard]] int32_t fail(int32_t step, int32_t error_number) noexcept;

        ghostlock::core::TcpInputs in;
        ghostlock::support::UniqueFd client_fd;
        ghostlock::support::UniqueFd server_fd;
        ghostlock::support::UniqueFd punch_fd;
        ghostlock::support::MappedRegion mapping;
        size_t mapping_length = 0;
        size_t page_size = 0;
        ghostlock::support::PthreadOwner punch_worker;
        std::atomic<int32_t> punch_go;
        std::atomic<int32_t> punch_stop;
        std::atomic<int32_t> punch_phase;
        std::atomic<int32_t> punch_failed;
        int32_t route_won = 0;
        ghostlock::route::RouteStatus status{};

    private:
        /* dirty failure: never close or unmap anything the puncher may still use */
        void retain_for_process_lifetime() noexcept;
    };
} // namespace ghostlock::route::tcp_zerocopy

#endif

#endif // GHOSTLOCK_CORE_ROUTE_TCP_H
