#include "routes/route_controller.h"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

static RouteStatus tcp_result;
static int tcp_calls;
static int select_calls;

namespace ghostlock::route {
    RouteStatus do_tcp_fake_lock_route(const WriteRequest *request) {
        assert(request);
        tcp_calls++;
        return tcp_result;
    }

    RouteStatus do_pselect_fake_lock_route(const WriteRequest *request) {
        assert(request);
        select_calls++;
        return (RouteStatus)
        {
            .code = ROUTE_OK,
            .userspace_clean = 1,
            .kernel_disarmed = 1,
        };
    }

    RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request) {
        assert(request);
        return (RouteStatus)
        {
            .code = ROUTE_OK
        };
    }
} // namespace ghostlock::route

static void reset_stubs(RouteStatus status) {
    tcp_result = status;
    tcp_calls = 0;
    select_calls = 0;
}

int main(void) {
    PiRaceContext race;
    WriteRequest request = {.mode = WriteMode::Zero};
    /* The optional fallback field is present: tcp failure falls back to select. */
    struct kernel_offsets values = {
        .route = kRouteTcpZerocopy,
        .fallback_route = kRouteSelectStack,
        .compact_waiter = 1,
    };
    TargetProfile profile = TargetProfile::from(&values);
    route::RouteController controller;
    route::route_controller_init(
        &controller, &race, &profile, route::RouteKind::TcpZerocopy);

    reset_stubs((RouteStatus)
    {
        .code = ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    });
    RouteStatus status = route::route_controller_execute(&controller, &request);
    assert(status.code == ROUTE_OK);
    assert(tcp_calls == 1 && select_calls == 1 && controller.fallback_used);

    /* Without the fallback field the tcp failure is returned unchanged. */
    struct kernel_offsets no_fallback = {
        .route = kRouteTcpZerocopy,
        .compact_waiter = 1,
    };
    TargetProfile no_fallback_profile = TargetProfile::from(&no_fallback);
    route::route_controller_init(
        &controller, &race, &no_fallback_profile, route::RouteKind::TcpZerocopy);
    reset_stubs((RouteStatus)
    {
        .code = ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    });
    status = route::route_controller_execute(&controller, &request);
    assert(status.code == ROUTE_FALLBACK_SAFE);
    assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

    route::route_controller_init(
        &controller, &race, &profile, route::RouteKind::TcpZerocopy);
    reset_stubs((RouteStatus)
    {
        .code = ROUTE_DIRTY_FAILURE,
        .userspace_clean = 0,
        .kernel_disarmed = 1,
    });
    status = route::route_controller_execute(&controller, &request);
    assert(status.code == ROUTE_DIRTY_FAILURE);
    assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

    puts("route_controller_test: ok");
    return 0;
}
