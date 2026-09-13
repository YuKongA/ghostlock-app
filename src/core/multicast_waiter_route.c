#include "multicast_waiter_route.h"

#include <string.h>

void multicast_waiter_route_context_init(
    MulticastWaiterRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    MulticastWaiterLayout layout,
    int resident) {
  memset(context, 0, sizeof(*context));
  context->race = race;
  context->request = request;
  context->execution = execution;
  context->layout = layout;
  context->socket_fd = -1;
  context->main_cpu = race ? race->main_cpu : 0;
  context->consumer_cpu = race ? race->consumer_cpu : 0;
  context->scheduler_policy = 0; /* SCHED_NORMAL/SCHED_OTHER */
  context->resident = resident;
  context->status.code = ROUTE_RETRYABLE;
  atomic_init(&context->waiter_has_lock2, 0);
  atomic_init(&context->owner_has_lock1, 0);
  atomic_init(&context->waiter_waiting, 0);
  atomic_init(&context->owner_waiting, 0);
  atomic_init(&context->waiter_ready, 0);
  atomic_init(&context->respray_requested, 0);
  atomic_init(&context->sprayed, 0);
  atomic_init(&context->stop_requested, 0);
  atomic_init(&context->owner_done, 0);
  atomic_init(&context->waiter_tid, 0);
}
