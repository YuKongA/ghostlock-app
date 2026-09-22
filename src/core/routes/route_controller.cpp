#include "routes/route_controller.h"

namespace ghostlock::route {
    using namespace ghostlock::profile;

    RouteStatus do_pselect_fake_lock_route(const WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request);

    void RouteController::init(ghostlock::race::PiRaceContext *race,
                               const TargetProfile *profile,
                               RouteKind selected) {
        *this = (RouteController)
        {
            .race = race,
            .profile = profile,
            .selected = selected,
            .allow_tcp_select_fallback = 1,
            .fallback_used = 0,
        };
    }

    RouteStatus RouteController::execute(const WriteRequest *request) {
        if (!race || !profile || !request) {
            return (RouteStatus)
            {
                .code = ROUTE_UNSUPPORTED
            };
        }
        switch (selected) {
            case RouteKind::MulticastWaiter:
                if (!profile->supports(ghostlock::profile::RouteKind::MulticastWaiter))
                    return (RouteStatus)
                {
                    .code = ROUTE_UNSUPPORTED
                };
                return do_kernel5_fake_lock_route(request);
            case RouteKind::TcpZerocopy: {
                if (!profile->supports(ghostlock::profile::RouteKind::TcpZerocopy))
                    return (RouteStatus)
                {
                    .code = ROUTE_UNSUPPORTED
                };
                RouteStatus status = do_tcp_fake_lock_route(request);
                if (status.code == ROUTE_OK || !allow_tcp_select_fallback ||
                    !status.can_fallback())
                    return status;
                /* Fallback follows the profile's explicit "fallback_to"
             * declaration; profiles without one return the TCP failure. */
                const struct kernel_offsets *values = profile->values();
                if (!values || values->fallback_route != kRouteSelectStack)
                    return status;
                fallback_used = 1;
                return do_pselect_fake_lock_route(request);
            }
            case RouteKind::SelectStack:
                if (!profile->supports(ghostlock::profile::RouteKind::SelectStack))
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
