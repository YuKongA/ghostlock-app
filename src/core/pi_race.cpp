#include "pi_race.h"

using namespace ghostlock;

void PiRace::reset(
        int initial_delay_usec, int main_cpu_value, int consumer_cpu_value) noexcept {
  /* Dropping a joinable owner here would detach its thread, matching the old
   * reset that simply zeroed the pthread_t and leaked it; every caller joins
   * through join() first, so this only runs on the normal path. */
  waiter_owner = PthreadOwner{};
  owner_owner = PthreadOwner{};
  consumer_owner = PthreadOwner{};
  wait_futex = 0;
  target_futex = 0;
  chain_futex = 0;
  atomic_store_explicit(&waiter_ready, 0, memory_order_relaxed);
  atomic_store_explicit(&waiter_waiting, 0, memory_order_relaxed);
  atomic_store_explicit(&owner_started, 0, memory_order_relaxed);
  atomic_store_explicit(&owner_chain_done, 0, memory_order_relaxed);
  atomic_store_explicit(&owner_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&route_done, 0, memory_order_relaxed);
  atomic_store_explicit(&waiter_tid, 0, memory_order_relaxed);
  atomic_store_explicit(&consumer_go, 0, memory_order_relaxed);
  atomic_store_explicit(&consumer_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&consumer_calls, 0, memory_order_relaxed);
  atomic_store_explicit(&consumer_success, 0, memory_order_relaxed);
  atomic_store_explicit(&consumer_inflight, 0, memory_order_relaxed);
  atomic_store_explicit(&route_delay_usec, initial_delay_usec,
                        memory_order_relaxed);
  atomic_store_explicit(&fast_repair, 0, memory_order_relaxed);
  main_cpu = main_cpu_value;
  consumer_cpu = consumer_cpu_value;
  request = nullptr;
  route_status = (RouteStatus) {.code = ROUTE_RETRYABLE};
}

int PiRace::start_threads(
        void *(*waiter_entry)(void *), void *(*owner_entry)(void *),
        void *(*consumer_entry)(void *), const WriteRequest *route_request) noexcept {
  if (!waiter_entry || !owner_entry || !consumer_entry) return EINVAL;
  request = route_request;
  int error = consumer_owner.start(consumer_entry, this);
  if (error) return error;
  error = owner_owner.start(owner_entry, this);
  if (error) {
    abort_startup();
    return error;
  }
  error = waiter_owner.start(waiter_entry, this);
  if (error) {
    abort_startup();
    return error;
  }
  return 0;
}

void PiRace::abort_startup() noexcept {
  atomic_store(&consumer_stop, 1);
  atomic_store(&owner_stop, 1);
  owner_owner.request_stop();
  consumer_owner.request_stop();
  (void) owner_owner.join();
  (void) consumer_owner.join();
  request = nullptr;
}

void PiRace::request_stop() noexcept {
  atomic_store(&consumer_go, 0);
  atomic_store(&consumer_stop, 1);
  atomic_store(&owner_stop, 1);
}

void PiRace::join() noexcept {
  (void) waiter_owner.join();
  (void) owner_owner.join();
  (void) consumer_owner.join();
  request = nullptr;
}

RouteStatus PiRace::outcome_with_counters(
        const RouteStatus &outcome, int calls, int success) noexcept {
  if (outcome.code == ROUTE_OK && (calls == 0 || success == 0)) {
    RouteStatus retryable = outcome;
    retryable.code = ROUTE_RETRYABLE;
    return retryable;
  }
  return outcome;
}
