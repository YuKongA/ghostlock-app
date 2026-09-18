#include "routes/route_controller.h"

#include <assert.h>
#include <stdio.h>

using namespace ghostlock;

static RouteStatus tcp_result;
static int tcp_calls;
static int select_calls;

namespace ghostlock::route {

RouteStatus do_tcp_fake_lock_route(const WriteRequest *request) {
  assert(request);
  tcp_calls++;
  return tcp_result;
}

RouteStatus do_pselect_fake_lock_route(const WriteRequest *request) {
  assert(request);
  select_calls++;
  return (RouteStatus){
      .code = ROUTE_OK,
      .userspace_clean = 1,
      .kernel_disarmed = 1,
  };
}

RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request) {
  assert(request);
  return (RouteStatus){.code = ROUTE_OK};
}

}  // namespace ghostlock::route

static void reset_stubs(RouteStatus status) {
  tcp_result = status;
  tcp_calls = 0;
  select_calls = 0;
}

int main(void) {
  PiRaceContext race;
  WriteRequest request = {.mode = WriteMode::Zero};
  struct kernel_offsets values = {.compact_waiter = 1};
  TargetProfile profile = target_profile_snapshot(&values);
  route::RouteController controller;
  route::route_controller_init(
      &controller, &race, &profile, route::RouteKind::TcpZerocopy);

  reset_stubs((RouteStatus){
      .code = ROUTE_FALLBACK_SAFE,
      .userspace_clean = 1,
      .kernel_disarmed = 1,
  });
  RouteStatus status = route::route_controller_execute(&controller, &request);
  assert(status.code == ROUTE_OK);
  assert(tcp_calls == 1 && select_calls == 1 && controller.fallback_used);

  route::route_controller_init(
      &controller, &race, &profile, route::RouteKind::TcpZerocopy);
  reset_stubs((RouteStatus){
      .code = ROUTE_DIRTY_FAILURE,
      .userspace_clean = 0,
      .kernel_disarmed = 1,
  });
  status = route::route_controller_execute(&controller, &request);
  assert(status.code == ROUTE_DIRTY_FAILURE);
  assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

  puts("route_controller_test: ok");
  return 0;
}
