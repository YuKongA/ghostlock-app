#include "multicast_waiter_route.h"

void multicast_waiter_route_context_init(
    MulticastWaiterRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    MulticastWaiterLayout layout,
    int resident) {
  context->race = race;
  context->request = request;
  context->execution = execution;
  context->layout = layout;
  context->lock1_futex = 0;
  context->lock2_futex = 0;
  context->condition_futex = 0;
  context->owner_worker = {};
  context->waiter_worker = {};
  context->owner_worker_started = 0;
  context->waiter_worker_started = 0;
  context->target = 0;
  context->value = 0;
  context->lock = 0;
  context->task = 0;
  context->socket_fd = -1;
  context->main_cpu = race ? race->main_cpu : 0;
  context->consumer_cpu = race ? race->consumer_cpu : 0;
  context->scheduler_policy = 0; /* SCHED_NORMAL/SCHED_OTHER */
  context->resident = resident;
  context->ready = 0;
  context->lock_slot = 0;
  context->status = {};
  context->status.code = ROUTE_RETRYABLE;
  atomic_store_explicit(&context->waiter_has_lock2, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_has_lock1, 0, memory_order_relaxed);
  atomic_store_explicit(&context->waiter_waiting, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_waiting, 0, memory_order_relaxed);
  atomic_store_explicit(&context->waiter_ready, 0, memory_order_relaxed);
  atomic_store_explicit(&context->respray_requested, 0, memory_order_relaxed);
  atomic_store_explicit(&context->sprayed, 0, memory_order_relaxed);
  atomic_store_explicit(&context->stop_requested, 0, memory_order_relaxed);
  atomic_store_explicit(&context->owner_done, 0, memory_order_relaxed);
  atomic_store_explicit(&context->waiter_tid, 0, memory_order_relaxed);
}
