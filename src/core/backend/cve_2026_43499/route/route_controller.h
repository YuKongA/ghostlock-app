#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H

#include "memory/payload_builder.h"
#include "race/pi_race.h"
#include "backend/cve_2026_43499/backend_profile/model.hpp"

namespace ghostlock::backend::cve_2026_43499::route {
    /* Thin entry over the route-policy registry: the profile picks the single
     * policy and the generic dispatcher runs it, so no per-route branch lives
     * here. There is no fallback (R6a): a failed route returns its failure. */
    struct RouteController {
        ghostlock::race::PiRace *race;
        const ghostlock::profile::TargetProfile *profile;

        void init(ghostlock::race::PiRace *race,
                  const ghostlock::profile::TargetProfile *profile);

        ghostlock::backend::cve_2026_43499::route::RouteStatus execute(const ghostlock::memory::WriteRequest *request);
    };
} // namespace ghostlock::backend::cve_2026_43499::route

#endif
