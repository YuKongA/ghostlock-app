#include "route/multicast_waiter_route.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace ghostlock;

int32_t main(void) {
    ghostlock::route::multicast_waiter::MulticastWaiterRoute context;
    ghostlock::race::PiRace race;
    race.main_cpu = 2;
    race.consumer_cpu = 3;
    ghostlock::memory::WriteRequest request = {0};
    ghostlock::profile::MulticastWaiterLayout layout = {
        .waiter_offset = 32, .buffer_size = 128,
        .task_offset = 40, .lock_offset = 48,
        .lock_slot_count = 4, .lock_slot_stride = 64,
    };
    const profile::TargetProfile profile{};

    context.init(&race, &request, &profile, layout, 1);
    assert(context.race == &race && context.request == &request);
    assert(context.profile == &profile);
    assert(context.layout.buffer_size == 128);
    assert(context.socket_fd == -1 && context.resident == 1);
    assert(context.main_cpu == 2 && context.consumer_cpu == 3);
    assert(context.scheduler_policy == 0);
    assert(!context.waiter_worker_started && !context.owner_worker_started);
    assert(context.stop_requested.load() == 0);
    assert(context.respray_requested.load() == 0);
    assert(context.status.code == ghostlock::route::ROUTE_RETRYABLE);
    puts("multicast_waiter_route_test: ok");
    return 0;
}
