#ifndef MULTICAST_WAITER_ROUTE_H
#define MULTICAST_WAITER_ROUTE_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "routes/route_status.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct MulticastWaiterRouteContext {
    PiRaceContext *race;
    const WriteRequest *request;
    const struct execution_settings *execution;
    MulticastWaiterLayout layout;
    uint32_t lock1_futex, lock2_futex, condition_futex;
    pthread_t owner_worker, waiter_worker;
    int owner_worker_started, waiter_worker_started;
    atomic_int waiter_has_lock2, owner_has_lock1;
    atomic_int waiter_waiting, owner_waiting, waiter_ready;
    atomic_int respray_requested, sprayed, stop_requested;
    atomic_int owner_done, waiter_tid;
    uintptr_t target, value, lock, task;
    int socket_fd;
    int ready, scheduler_policy, lock_slot;
    int main_cpu, consumer_cpu;
    int resident;
    RouteStatus status;
} MulticastWaiterRouteContext;

void multicast_waiter_route_context_init(
        MulticastWaiterRouteContext *context,
        PiRaceContext *race,
        const WriteRequest *request,
        const struct execution_settings *execution,
        MulticastWaiterLayout layout,
        int resident);

#endif
