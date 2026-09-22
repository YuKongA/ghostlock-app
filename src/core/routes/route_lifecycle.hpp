#ifndef GHOSTLOCK_ROUTE_LIFECYCLE_HPP
#define GHOSTLOCK_ROUTE_LIFECYCLE_HPP

#include <concepts>

#include "routes/route_status.h"

namespace ghostlock::route {

/* Structural contract shared by the route classes. Expressed as a concept
 * rather than a virtual base: the lifecycle runs inside the PI race window,
 * so no vtable or indirect dispatch may enter the path. */
template <class R>
concept RouteLifecycle = requires(R &route) {
    { route.prepare() } -> std::convertible_to<int>;
    { route.execute() } -> std::same_as<RouteStatus>;
    route.disarm();
    route.destroy();
    { route.status } -> std::convertible_to<RouteStatus>;
};

/* The validated order: prepare -> execute (only when prepare succeeded) ->
 * disarm -> destroy. Returns the outcome the route published in `status`. */
template <RouteLifecycle R>
RouteStatus run_route_lifecycle(R &route) {
    if (route.prepare() == 0) {
        (void) route.execute();
    }
    route.disarm();
    route.destroy();
    return route.status;
}

} // namespace ghostlock::route

#endif
