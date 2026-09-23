/* Host test for the route lifecycle contract: the concept accepts exactly the
 * required shape, and run_route_lifecycle fixes the prepare/execute/disarm/
 * destroy order with its gating rule. */

#include "route/route_lifecycle.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace ghostlock;

namespace {
    struct FakeRoute {
        int32_t prepare_result = 0;
        bool fail_prepare = false;
        int32_t prepare_calls = 0;
        int32_t execute_calls = 0;
        int32_t disarm_calls = 0;
        int32_t destroy_calls = 0;
        std::string order;
        route::RouteStatus status{.code = route::ROUTE_RETRYABLE};

        [[nodiscard]] int32_t prepare() noexcept {
            prepare_calls++;
            order += "P";
            if (fail_prepare) {
                status.code = route::ROUTE_DIRTY_FAILURE;
                status.step = 7;
                return 1;
            }
            return prepare_result;
        }

        [[nodiscard]] route::RouteStatus execute() noexcept {
            execute_calls++;
            order += "E";
            status = {.code = route::ROUTE_OK, .userspace_clean = 1, .kernel_disarmed = 1};
            return status;
        }

        void disarm() noexcept {
            disarm_calls++;
            order += "D";
        }

        void destroy() noexcept {
            destroy_calls++;
            order += "X";
        }
    };

    /* Missing destroy(): the structural contract must reject it. */
    struct IncompleteRoute {
        [[nodiscard]] int32_t prepare() noexcept { return 0; }
        [[nodiscard]] route::RouteStatus execute() noexcept { return status; }
        void disarm() noexcept {
        }

        route::RouteStatus status{};
    };

    static_assert(route::RouteLifecycle<FakeRoute>);
    static_assert(!route::RouteLifecycle<IncompleteRoute>);
} // namespace

int32_t main(void) {
    using namespace ghostlock::route;

    /* Prepare succeeds: the full sequence runs once, in order, and the route's
     * published status is returned. */
    {
        FakeRoute route;
        const RouteStatus status = run_route_lifecycle(route);
        assert(status.code == ROUTE_OK);
        assert(route.prepare_calls == 1 && route.execute_calls == 1);
        assert(route.disarm_calls == 1 && route.destroy_calls == 1);
        assert(route.order == "PEDX");
    }

    /* Prepare fails: execute is skipped, disarm/destroy still run, and the
     * route's own failure status is returned. */
    {
        FakeRoute route;
        route.fail_prepare = true;
        const RouteStatus status = run_route_lifecycle(route);
        assert(status.code == ROUTE_DIRTY_FAILURE && status.step == 7);
        assert(route.prepare_calls == 1 && route.execute_calls == 0);
        assert(route.disarm_calls == 1 && route.destroy_calls == 1);
        assert(route.order == "PDX");
    }

    /* A non-zero prepare code is a plain integer failure, not a status code. */
    {
        FakeRoute route;
        route.prepare_result = 4;
        assert(run_route_lifecycle(route).code == ROUTE_RETRYABLE);
        assert(route.execute_calls == 0 && route.order == "PDX");
    }

    puts("route_lifecycle_test: ok");
    return 0;
}
