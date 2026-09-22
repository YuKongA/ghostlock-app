#ifndef PI_RACE_H
#define PI_RACE_H

#include "memory/payload_builder.h"
#include "routes/route_status.h"
#include "support/native_resource.hpp"

#include <pthread.h>
#include <atomic>
#include <cstdint>

namespace ghostlock::race {
    /* Owns one PI race attempt: the two PI futexes the waiter walks, the three
     * worker threads and the route outcome. The atomics stay public because the
     * route operations read and write them from their hot loops; this class owns
     * the lifecycle that used to be spread across main.cpp helpers: creation
     * order (consumer -> owner -> waiter), the stop signal and the join order
     * (waiter -> owner -> consumer). */
    class PiRace final {
    public:
        PiRace() noexcept = default;

        PiRace(const PiRace &) = delete;

        PiRace &operator=(const PiRace &) = delete;

        ~PiRace() noexcept = default;

        /* All state returns to its reset value. The fast_repair latch is owned by
         * the caller (reset_main_route_state preserves it across resets). */
        void reset(int initial_delay_usec, int main_cpu, int consumer_cpu) noexcept;

        /* consumer -> owner -> waiter, matching the original creation order. On a
         * partial failure every already-started worker is asked to stop and joined
         * before the pthread_create error is returned. */
        [[nodiscard]] int start_threads(void *(*waiter_entry)(void *),
                                        void *(*owner_entry)(void *),
                                        void *(*consumer_entry)(void *),
                                        const ghostlock::memory::WriteRequest *route_request) noexcept;

        /* Waits for the route to finish and returns its outcome. A route that
         * reports Ok without a consumer call and success has not won the race, so
         * the returned outcome degrades to Retryable, matching the bool the
         * previous free function returned. The wait itself is unbounded today:
         * see TODO(pi-timeout-01). */
        [[nodiscard]] ghostlock::route::RouteStatus run() noexcept;

        /* Business stop signal for the three workers; idempotent. */
        void request_stop() noexcept;

        /* waiter -> owner -> consumer join order; idempotent. */
        void join() noexcept;

        /* Testable counter merge used by run(): Ok with zero calls or zero
         * successes is not a routed attempt. */
        [[nodiscard]] static ghostlock::route::RouteStatus outcome_with_counters(
            const ghostlock::route::RouteStatus &outcome, int calls, int success) noexcept;

        /* C atomics cannot carry initializers in C++; static instances are
         * zero-initialized and every dynamic path calls reset() first.
         * Memory order: the hot-path stores/loads keep the original default
         * (sequentially consistent) semantics and reset() keeps the previous
         * relaxed stores. Do not relax either without a dedicated race gate. */
        uint32_t wait_futex = 0;
        uint32_t target_futex = 0;
        uint32_t chain_futex = 0;
        std::atomic<int> waiter_ready;
        std::atomic<int> waiter_waiting;
        std::atomic<int> owner_started;
        std::atomic<int> owner_chain_done;
        std::atomic<int> owner_stop;
        std::atomic<int> route_done;
        std::atomic<int> waiter_tid;
        std::atomic<int> consumer_go;
        std::atomic<int> consumer_stop;
        std::atomic<int> consumer_calls;
        std::atomic<int> consumer_success;
        std::atomic<int> consumer_inflight;
        std::atomic<int> route_delay_usec;
        std::atomic<int> fast_repair;
        int main_cpu = 0;
        int consumer_cpu = 0;
        ghostlock::support::PthreadOwner waiter_owner;
        ghostlock::support::PthreadOwner owner_owner;
        ghostlock::support::PthreadOwner consumer_owner;
        const ghostlock::memory::WriteRequest *request = nullptr;
        ghostlock::route::RouteStatus route_status{};

    private:
        void abort_startup() noexcept;
    };
} // namespace ghostlock::race

#endif
