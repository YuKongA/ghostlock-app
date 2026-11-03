#include "route/route_controller.h"

#include "route/route_policy.hpp"

namespace ghostlock::route {
    void RouteController::init(ghostlock::race::PiRace *race_ptr,
                               const profile::TargetProfile *profile_ptr) {
        race = race_ptr;
        profile = profile_ptr;
        fallback_used = 0;
    }

    RouteStatus RouteController::execute(const memory::WriteRequest *request) {
        if (!race || !profile || !request) {
            return RouteStatus{.code = ROUTE_UNSUPPORTED};
        }
        const RouteRunResult result = run_route(*profile, request, 1);
        fallback_used = result.fallback_used ? 1 : 0;
        return result.status;
    }
} // namespace ghostlock::route
