#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H

#include "memory/payload_builder.h"
#include "race/pi_race.h"
#include "profile/model.h"

namespace lsprivilege::route {
    /* Thin entry over the route-policy registry: the profile picks the policy
     * and the generic dispatcher runs it (with the declared fallback), so no
     * per-route branch lives here. */
    struct RouteController {
        lsprivilege::race::PiRace *race;
        const lsprivilege::profile::TargetProfile *profile;
        /* Kept as int: the field narrows this struct and measurably moves the
         * waiter_thread stack offsets (CPP17 experiment). */
        int32_t fallback_used;

        void init(lsprivilege::race::PiRace *race,
                  const lsprivilege::profile::TargetProfile *profile);

        lsprivilege::route::RouteStatus execute(const lsprivilege::memory::WriteRequest *request);
    };
} // namespace lsprivilege::route

#endif
