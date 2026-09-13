#include "pi_race.h"

#include <string.h>

void pi_race_reset(
    PiRaceContext *context, int initial_delay_usec,
    int main_cpu, int consumer_cpu) {
  memset(context, 0, sizeof(*context));
  atomic_init(&context->waiter_ready, 0);
  atomic_init(&context->waiter_waiting, 0);
  atomic_init(&context->owner_started, 0);
  atomic_init(&context->owner_chain_done, 0);
  atomic_init(&context->owner_stop, 0);
  atomic_init(&context->route_done, 0);
  atomic_init(&context->waiter_tid, 0);
  atomic_init(&context->consumer_go, 0);
  atomic_init(&context->consumer_stop, 0);
  atomic_init(&context->consumer_calls, 0);
  atomic_init(&context->consumer_success, 0);
  atomic_init(&context->consumer_inflight, 0);
  atomic_init(&context->route_delay_usec, initial_delay_usec);
  atomic_init(&context->fast_repair, 0);
  context->main_cpu = main_cpu;
  context->consumer_cpu = consumer_cpu;
  context->route_status = (RouteStatus){.code = ROUTE_RETRYABLE};
}
