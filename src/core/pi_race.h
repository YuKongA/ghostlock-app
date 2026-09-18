#ifndef PI_RACE_H
#define PI_RACE_H

#include "memory/payload_builder.h"
#include "routes/route_status.h"
#include "support/native_resource.hpp"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

namespace ghostlock {

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
                                  const WriteRequest *route_request) noexcept;

  /* Waits for the route to finish and returns its outcome. A route that
   * reports Ok without a consumer call and success has not won the race, so
   * the returned outcome degrades to Retryable, matching the bool the
   * previous free function returned. The wait itself is unbounded today:
   * see TODO(pi-timeout-01). */
  [[nodiscard]] RouteStatus run() noexcept;

  /* Business stop signal for the three workers; idempotent. */
  void request_stop() noexcept;

  /* waiter -> owner -> consumer join order; idempotent. */
  void join() noexcept;

  /* Testable counter merge used by run(): Ok with zero calls or zero
   * successes is not a routed attempt. */
  [[nodiscard]] static RouteStatus outcome_with_counters(
          const RouteStatus &outcome, int calls, int success) noexcept;

  /* C atomics cannot carry initializers in C++; static instances are
   * zero-initialized and every dynamic path calls reset() first.
   * Memory order: the hot-path stores/loads keep the original default
   * (sequentially consistent) semantics and reset() keeps the previous
   * relaxed stores. Do not relax either without a dedicated race gate. */
  uint32_t wait_futex = 0;
  uint32_t target_futex = 0;
  uint32_t chain_futex = 0;
  atomic_int waiter_ready;
  atomic_int waiter_waiting;
  atomic_int owner_started;
  atomic_int owner_chain_done;
  atomic_int owner_stop;
  atomic_int route_done;
  atomic_int waiter_tid;
  atomic_int consumer_go;
  atomic_int consumer_stop;
  atomic_int consumer_calls;
  atomic_int consumer_success;
  atomic_int consumer_inflight;
  atomic_int route_delay_usec;
  atomic_int fast_repair;
  int main_cpu = 0;
  int consumer_cpu = 0;
  PthreadOwner waiter_owner;
  PthreadOwner owner_owner;
  PthreadOwner consumer_owner;
  const WriteRequest *request = nullptr;
  RouteStatus route_status{};

 private:
  void abort_startup() noexcept;
};

}  // namespace ghostlock

using PiRaceContext = ghostlock::PiRace;
extern PiRaceContext &g_pi_race_context;

#endif
