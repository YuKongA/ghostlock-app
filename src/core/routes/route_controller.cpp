#include "routes/route_controller.h"

namespace ghostlock::route {
    RouteStatus do_pselect_fake_lock_route(const WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request);

    void route_controller_init(RouteController *controller,
                               PiRaceContext *race,
                               const TargetProfile *profile,
                               RouteKind selected) {
        *controller = (RouteController)
        {
            .race = race,
            .profile = profile,
            .selected = selected,
            .allow_tcp_select_fallback = 1,
            .fallback_used = 0,
        };
    }

    RouteStatus route_controller_execute(RouteController *controller,
                                         const WriteRequest *request) {
        if (!controller || !controller->race || !controller->profile || !request) {
            return (RouteStatus)
            {
                .code = ROUTE_UNSUPPORTED
            };
        }
        switch (controller->selected) {
            case RouteKind::MulticastWaiter:
                if (!controller->profile->supports(::RouteKind::MulticastWaiter))
                    return (RouteStatus)
                {
                    .code = ROUTE_UNSUPPORTED
                };
                return do_kernel5_fake_lock_route(request);
            case RouteKind::TcpZerocopy: {
                if (!controller->profile->supports(::RouteKind::TcpZerocopy))
                    return (RouteStatus)
                {
                    .code = ROUTE_UNSUPPORTED
                };
                RouteStatus status = do_tcp_fake_lock_route(request);
                if (status.code == ROUTE_OK || !controller->allow_tcp_select_fallback ||
                    !route_status_allows_fallback(&status))
                    return status;
                /* Fallback follows the profile's explicit "fallback_to"
             * declaration; profiles without one return the TCP failure. */
                const struct kernel_offsets *values =
                        controller->profile->values();
                if (!values || values->fallback_route != kRouteSelectStack)
                    return status;
                controller->fallback_used = 1;
                return do_pselect_fake_lock_route(request);
            }
            case RouteKind::SelectStack:
                if (!controller->profile->supports(::RouteKind::SelectStack))
                    return (RouteStatus)
                {
                    .code = ROUTE_UNSUPPORTED
                };
                return do_pselect_fake_lock_route(request);
        }
        return (RouteStatus)
        {
            .code = ROUTE_UNSUPPORTED
        };
    }
} // namespace ghostlock::route
