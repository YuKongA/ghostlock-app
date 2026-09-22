#ifndef TCP_ZEROCOPY_ROUTE_H
#define TCP_ZEROCOPY_ROUTE_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "routes/route_status.h"
#include "support/native_resource.hpp"

#include <pthread.h>
#include <atomic>
#include <cstddef>

namespace ghostlock {
    /* Owns every userspace resource of one TCP zerocopy attempt: the loopback
 * pair, the punching memfd and its shared mapping, and the punch worker.
 * Lifetime is explicit: prepare() -> execute() -> disarm() -> destroy().
 * Driver fields stay public because route_operations.cpp runs the trigger
 * directly from the hot loop and a punch worker trampoline reads the atomics.
 * A dirty failure deliberately retains the resources for process lifetime so
 * the still-running puncher never sees a recycled descriptor. */
    class TcpZerocopyRoute final {
    public:
        TcpZerocopyRoute(PiRace *race, const WriteRequest *request,
                         const execution_settings *execution,
                         size_t mapping_length) noexcept;

        ~TcpZerocopyRoute() noexcept = default;

        TcpZerocopyRoute(const TcpZerocopyRoute &) = delete;

        TcpZerocopyRoute &operator=(const TcpZerocopyRoute &) = delete;

        TcpZerocopyRoute(TcpZerocopyRoute &&other) noexcept;

        /* Acquire every userspace resource. No PI consumer or punch operation is
   * armed until this returns 0; every failure records step/error_number. */
        [[nodiscard]] int prepare() noexcept;

        /* Run the trigger after prepare() established exclusive ownership. */
        [[nodiscard]] RouteStatus execute() noexcept;

        /* Stop every trigger and drain the consumer. */
        void disarm() noexcept;

        /* Join the worker, then release each owned resource once. Join or munmap
   * failures mark the attempt dirty and retain the remaining resources for
   * process lifetime, matching the C implementation. */
        void destroy() noexcept;

        [[nodiscard]] int fail(int step, int error_number) noexcept;

        PiRace *race = nullptr;
        const WriteRequest *request = nullptr;
        const struct execution_settings *execution = nullptr;
        UniqueFd client_fd;
        UniqueFd server_fd;
        UniqueFd punch_fd;
        MappedRegion mapping;
        size_t mapping_length = 0;
        size_t page_size = 0;
        PthreadOwner punch_worker;
        std::atomic<int> punch_go;
        std::atomic<int> punch_stop;
        std::atomic<int> punch_phase;
        std::atomic<int> punch_failed;
        int route_won = 0;
        RouteStatus status{};

    private:
        /* dirty failure: never close or unmap anything the puncher may still use */
        void retain_for_process_lifetime() noexcept;
    };
} // namespace ghostlock

using TcpZerocopyRouteContext = ghostlock::TcpZerocopyRoute;

#endif
