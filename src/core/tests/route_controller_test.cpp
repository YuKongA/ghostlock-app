#include "route/route_controller.h"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

static ghostlock::route::RouteStatus tcp_result;
static int32_t tcp_calls;
static int32_t select_calls;

namespace ghostlock::route {
    RouteStatus do_tcp_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        tcp_calls++;
        return tcp_result;
    }

    RouteStatus do_pselect_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        select_calls++;
        return (RouteStatus)
        {
            .code = ROUTE_OK,
            .userspace_clean = 1,
            .kernel_disarmed = 1,
        };
    }

    RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        return (RouteStatus)
        {
            .code = ROUTE_OK
        };
    }
} // namespace ghostlock::route

static void reset_stubs(ghostlock::route::RouteStatus status) {
    tcp_result = status;
    tcp_calls = 0;
    select_calls = 0;
}

int32_t main(void) {
    ghostlock::race::PiRace race;
    ghostlock::memory::WriteRequest request = {.mode = ghostlock::memory::WriteMode::Zero};
    /* The optional fallback field is present: tcp failure falls back to select. */
    profile::kernel_offsets values = {
        .route = ghostlock::profile::kRouteTcpZerocopy,
        .fallback_route = ghostlock::profile::kRouteSelectStack,
        .compact_waiter = 1,
    };
    ghostlock::profile::TargetProfile profile = ghostlock::profile::TargetProfile::from(&values);
    route::RouteController controller;
    controller.init(&race, &profile);

    reset_stubs((ghostlock::route::RouteStatus)
    {
        .code = ghostlock::route::ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    });
    ghostlock::route::RouteStatus status = controller.execute(&request);
    assert(status.code == ghostlock::route::ROUTE_OK);
    assert(tcp_calls == 1 && select_calls == 1 && controller.fallback_used);

    /* Without the fallback field the tcp failure is returned unchanged. */
    profile::kernel_offsets no_fallback = {
        .route = ghostlock::profile::kRouteTcpZerocopy,
        .compact_waiter = 1,
    };
    ghostlock::profile::TargetProfile no_fallback_profile = ghostlock::profile::TargetProfile::from(&no_fallback);
    controller.init(&race, &no_fallback_profile);
    reset_stubs((ghostlock::route::RouteStatus)
    {
        .code = ghostlock::route::ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    });
    status = controller.execute(&request);
    assert(status.code == ghostlock::route::ROUTE_FALLBACK_SAFE);
    assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

    controller.init(&race, &profile);
    reset_stubs((ghostlock::route::RouteStatus)
    {
        .code = ghostlock::route::ROUTE_DIRTY_FAILURE,
        .userspace_clean = 0,
        .kernel_disarmed = 1,
    });
    status = controller.execute(&request);
    assert(status.code == ghostlock::route::ROUTE_DIRTY_FAILURE);
    assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

    puts("route_controller_test: ok");
    return 0;
}
