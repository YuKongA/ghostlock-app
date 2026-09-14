#include "pi_race.h"

void pi_race_reset(
    PiRaceContext *context, int initial_delay_usec,
    int main_cpu, int consumer_cpu) {
  context->wait_futex = 0;
  context->target_futex = 0;
  context->chain_futex = 0;
  atomic_store_explicit(&context->waiter_ready, 0, memory_order_relaxed);
  atomic_store_explicit(&context->waiter_waiting, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_started, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_chain_done, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&context->route_done, 0, memory_order_relaxed);
  atomic_store_explicit(&context->waiter_tid, 0, memory_order_relaxed);
  atomic_store_explicit(&context->consumer_go, 0, memory_order_relaxed);
  atomic_store_explicit(&context->consumer_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&context->consumer_calls, 0, memory_order_relaxed);
  atomic_store_explicit(&context->consumer_success, 0, memory_order_relaxed);
  atomic_store_explicit(&context->consumer_inflight, 0, memory_order_relaxed);
  atomic_store_explicit(&context->route_delay_usec, initial_delay_usec,
                        memory_order_relaxed);
  atomic_store_explicit(&context->fast_repair, 0, memory_order_relaxed);
  context->main_cpu = main_cpu;
  context->consumer_cpu = consumer_cpu;
  context->waiter_thread = {};
  context->owner_thread = {};
  context->consumer_thread = {};
  context->waiter_started = 0;
  context->owner_started_thread = 0;
  context->consumer_started = 0;
  context->request = nullptr;
  context->route_status = (RouteStatus){.code = ROUTE_RETRYABLE};
}
