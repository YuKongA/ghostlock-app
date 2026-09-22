#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"

namespace ghostlock::route {
    enum class RouteKind : int {
        MulticastWaiter = 0,
        TcpZerocopy,
        SelectStack,
    };

    typedef struct RouteController {
        PiRaceContext *race;
        const TargetProfile *profile;
        RouteKind selected;
        /* Kept as int: bool fields shrink this struct and measurably move the
     * waiter_thread stack offsets (CPP17 experiment, 886 instructions
     * reshuffled). The style gain does not justify a stack-layout change. */
        int allow_tcp_select_fallback;
        int fallback_used;
    } RouteController;

    void route_controller_init(RouteController *controller,
                               PiRaceContext *race,
                               const TargetProfile *profile,
                               RouteKind selected);

    static inline int route_status_allows_fallback(const RouteStatus *status) {
        return status && status->can_fallback();
    }

    RouteStatus route_controller_execute(RouteController *controller,
                                         const WriteRequest *request);
} // namespace ghostlock::route

#endif

