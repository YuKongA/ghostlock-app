#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H

#include "memory/payload_builder.h"
#include "race/pi_race.h"
#include "profile/model.h"

namespace ghostlock::route {
    /* Thin entry over the route-policy registry: the profile picks the policy
     * and the generic dispatcher runs it (with the declared fallback), so no
     * per-route branch lives here. */
    struct RouteController {
        ghostlock::race::PiRace *race;
        const ghostlock::profile::TargetProfile *profile;
        /* Kept as int: the field narrows this struct and measurably moves the
         * waiter_thread stack offsets (CPP17 experiment). */
        int32_t fallback_used;

        void init(ghostlock::race::PiRace *race,
                  const ghostlock::profile::TargetProfile *profile);

        ghostlock::route::RouteStatus execute(const ghostlock::memory::WriteRequest *request);
    };
} // namespace ghostlock::route

#endif
