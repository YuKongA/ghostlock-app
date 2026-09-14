#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H

#include "memory/payload_builder.h"
#include "pi_race.h"
#include "profile.h"

typedef enum RouteKind {
    ROUTE_KIND_MULTICAST_WAITER = 0,
    ROUTE_KIND_TCP_ZEROCOPY,
    ROUTE_KIND_SELECT_STACK,
} RouteKind;

typedef struct RouteController {
    PiRaceContext *race;
    const TargetProfile *profile;
    RouteKind selected;
    int allow_tcp_select_fallback;
    int fallback_used;
} RouteController;

void route_controller_init(RouteController *controller,
        PiRaceContext *race,
        const TargetProfile *profile,
        RouteKind selected);

static inline int route_status_allows_fallback(const RouteStatus *status) {
    return status && status->code == ROUTE_FALLBACK_SAFE &&
            status->userspace_clean && status->kernel_disarmed;
}

RouteStatus route_controller_execute(RouteController *controller,
        const WriteRequest *request);

#endif
