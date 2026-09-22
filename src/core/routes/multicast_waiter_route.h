#ifndef MULTICAST_WAITER_ROUTE_H
#define MULTICAST_WAITER_ROUTE_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "routes/route_status.h"

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
 * route_operations.cpp forward to this class. */
namespace ghostlock::route::multicast_waiter {

class MulticastWaiterRoute final {
public:
    MulticastWaiterRoute() noexcept = default;

    ~MulticastWaiterRoute() noexcept = default;

    MulticastWaiterRoute(const MulticastWaiterRoute &) = delete;

    MulticastWaiterRoute &operator=(const MulticastWaiterRoute &) = delete;

    /* Host-safe: the fixed-vector test constructs a context and calls init()
   * without linking the Android-only implementation unit. */
    void init(ghostlock::race::PiRace *race_value, const ghostlock::memory::WriteRequest *request_value,
              const struct ghostlock::profile::execution_settings *execution_value,
              ghostlock::profile::MulticastWaiterLayout layout_value, int resident_value) noexcept {
        race = race_value;
        request = request_value;
        execution = execution_value;
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
    [[nodiscard]] int start() noexcept;

    [[nodiscard]] int write(uintptr_t target, uintptr_t value) noexcept;

    void stop() noexcept;

    /* Fields stay public: the workers read and write them from their hot loops,
   * preserving the pre-class access shape and field order. */
    ghostlock::race::PiRace *race;
    const ghostlock::memory::WriteRequest *request;
    const struct ghostlock::profile::execution_settings *execution;
    ghostlock::profile::MulticastWaiterLayout layout;
    uint32_t lock1_futex, lock2_futex, condition_futex;
    pthread_t owner_worker, waiter_worker;
    int owner_worker_started, waiter_worker_started;
    std::atomic<int> waiter_has_lock2, owner_has_lock1;
    std::atomic<int> waiter_waiting, owner_waiting, waiter_ready;
    std::atomic<int> respray_requested, sprayed, stop_requested;
    std::atomic<int> owner_done, waiter_tid;
    uintptr_t target, value, lock, task;
    int socket_fd;
    int ready, scheduler_policy, lock_slot;
    int main_cpu, consumer_cpu;
    int resident;
    ghostlock::route::RouteStatus status;
};

/* Process-level resident owner. */
MulticastWaiterRoute &resident_route(void);

} // namespace ghostlock::route::multicast_waiter

#endif
