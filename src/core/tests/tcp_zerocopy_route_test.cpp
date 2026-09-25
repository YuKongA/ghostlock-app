#include "route/tcp_zerocopy_route.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <type_traits>

using namespace ghostlock;


int32_t main(void) {
    ghostlock::race::PiRace race;
    assert(race.reset(0, 0, 1));
    ghostlock::memory::WriteRequest request{};
    const profile::TargetProfile profile{};

    ghostlock::route::tcp_zerocopy::TcpZerocopyRoute context(&race, &request, profile, 16 * 1024 * 1024);
    assert(context.race == &race);
    assert(context.request == &request);
    assert(&context.profile == &profile);
    assert(!context.client_fd.valid());
    assert(!context.server_fd.valid());
    assert(!context.punch_fd.valid());
    assert(!context.mapping.valid());
    assert(context.mapping_length == 16 * 1024 * 1024);
    assert(context.punch_worker.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(context.punch_go.load() == 0);
    assert(context.punch_stop.load() == 0);
    assert(context.punch_phase.load() == 0);
    assert(context.punch_failed.load() == 0);
    assert(context.status.code == ghostlock::route::ROUTE_RETRYABLE);
    assert(context.status.userspace_clean == 0);
    assert(context.status.kernel_disarmed == 0);

    /* Move-only: no copy, resources transfer with the move. */
    static_assert(!std::is_copy_constructible_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);
    static_assert(!std::is_copy_assignable_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);
    static_assert(!std::is_move_constructible_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);

    /* disarm is idempotent and stops the shared consumer trigger. */
    context.disarm();
    context.disarm();
    assert(context.status.kernel_disarmed == 1);
    assert(race.consumer_go.load() == 0);

    /* destroy without resources is idempotent and reports a clean fallback. */
    context.destroy();
    context.destroy();
    assert(context.status.userspace_clean == 1);
    assert(context.status.code == ghostlock::route::ROUTE_FALLBACK_SAFE);

    /* fail() records step and errno for the caller's log. */
    assert(context.fail(59, 5) == -1);
    assert(context.status.step == 59);
    assert(context.status.error_number == 5);

    puts("tcp_zerocopy_route_test: ok");
    return 0;
}
