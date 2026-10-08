/* R6a: the RouteController runs exactly the profile's one route. A clean or
 * dirty tcp failure is returned unchanged; the select stub is never reached,
 * because the fallback field no longer exists on the wire or in the model. */

#include "backend/cve_2026_43499/route/route_controller.h"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

static ghostlock::backend::cve_2026_43499::route::RouteStatus tcp_result;
static int32_t tcp_calls;
static int32_t select_calls;

namespace ghostlock::backend::cve_2026_43499::route {
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
} // namespace ghostlock::backend::cve_2026_43499::route

static void reset_stubs(ghostlock::backend::cve_2026_43499::route::RouteStatus status) {
    tcp_result = status;
    tcp_calls = 0;
    select_calls = 0;
}

int32_t main(void) {
    ghostlock::race::PiRace race;
    ghostlock::memory::WriteRequest request = {.mode = ghostlock::memory::WriteMode::Zero};
    profile::kernel_offsets values = {
        .route = ghostlock::profile::kRouteTcpZerocopy,
        .misc = {.compact_waiter = 1},
    };
    ghostlock::profile::TargetProfile profile = ghostlock::profile::TargetProfile::from(&values);
    ghostlock::backend::cve_2026_43499::route::RouteController controller;
    controller.init(&race, &profile);

    /* A clean tcp failure stays put: no second route runs. */
    reset_stubs((ghostlock::backend::cve_2026_43499::route::RouteStatus)
    {
        .code = ghostlock::backend::cve_2026_43499::route::ROUTE_FALLBACK_SAFE,
        .userspace_clean = 1,
        .kernel_disarmed = 1,
    });
    ghostlock::backend::cve_2026_43499::route::RouteStatus status = controller.execute(&request);
    assert(status.code == ghostlock::backend::cve_2026_43499::route::ROUTE_FALLBACK_SAFE);
    assert(tcp_calls == 1 && select_calls == 0);

    /* A dirty tcp failure stays put too. */
    reset_stubs((ghostlock::backend::cve_2026_43499::route::RouteStatus)
    {
        .code = ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE,
        .userspace_clean = 0,
        .kernel_disarmed = 1,
    });
    status = controller.execute(&request);
    assert(status.code == ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE);
    assert(tcp_calls == 1 && select_calls == 0);

    /* A route-less (None) profile is unsupported and dispatches nothing. */
    profile::kernel_offsets none_values = {
        .route = ghostlock::profile::kRouteNone,
    };
    ghostlock::profile::TargetProfile none_profile =
        ghostlock::profile::TargetProfile::from(&none_values);
    controller.init(&race, &none_profile);
    reset_stubs((ghostlock::backend::cve_2026_43499::route::RouteStatus)
    {
        .code = ghostlock::backend::cve_2026_43499::route::ROUTE_OK
    });
    status = controller.execute(&request);
    assert(status.code == ghostlock::backend::cve_2026_43499::route::ROUTE_UNSUPPORTED);
    assert(tcp_calls == 0 && select_calls == 0);

    puts("route_controller_test: ok");
    return 0;
}
