#include "backend/cve_2026_43499/route/route_controller.h"

#include "backend/cve_2026_43499/route/route_policy.hpp"

namespace ghostlock::backend::cve_2026_43499::route {
    void RouteController::init(ghostlock::race::PiRace *race_ptr,
                               const profile::TargetProfile *profile_ptr) {
        race = race_ptr;
        profile = profile_ptr;
    }

    RouteStatus RouteController::execute(const memory::WriteRequest *request) {
        if (!race || !profile || !request) {
            return RouteStatus{.code = ROUTE_UNSUPPORTED};
        }
        return run_route(*profile, request);
    }
} // namespace ghostlock::backend::cve_2026_43499::route
