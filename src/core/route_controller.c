#include "route_controller.h"

RouteStatus do_pselect_fake_lock_route(const WriteRequest *request);
RouteStatus do_tcp_fake_lock_route(const WriteRequest *request);
RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request);

void route_controller_init(RouteController *controller,
                           PiRaceContext *race,
                           const TargetProfile *profile,
                           RouteKind selected) {
  *controller = (RouteController){
      .race = race,
      .profile = profile,
      .selected = selected,
      .allow_tcp_select_fallback = 1,
  };
}

RouteStatus route_controller_execute(RouteController *controller,
                                     const WriteRequest *request) {
  if (!controller || !controller->race || !controller->profile || !request) {
    return (RouteStatus){.code = ROUTE_UNSUPPORTED};
  }
  switch (controller->selected) {
    case ROUTE_KIND_MULTICAST_WAITER:
      if (!target_profile_supports_multicast_waiter(controller->profile))
        return (RouteStatus){.code = ROUTE_UNSUPPORTED};
      return do_kernel5_fake_lock_route(request);
    case ROUTE_KIND_TCP_ZEROCOPY: {
      if (!target_profile_supports_tcp_zerocopy(controller->profile))
        return (RouteStatus){.code = ROUTE_UNSUPPORTED};
      RouteStatus status = do_tcp_fake_lock_route(request);
      if (status.code == ROUTE_OK || !controller->allow_tcp_select_fallback ||
          !route_status_allows_fallback(&status)) return status;
      if (!target_profile_supports_select_stack(controller->profile))
        return status;
      controller->fallback_used = 1;
      return do_pselect_fake_lock_route(request);
    }
    case ROUTE_KIND_SELECT_STACK:
      if (!target_profile_supports_select_stack(controller->profile))
        return (RouteStatus){.code = ROUTE_UNSUPPORTED};
      return do_pselect_fake_lock_route(request);
  }
  return (RouteStatus){.code = ROUTE_UNSUPPORTED};
}
