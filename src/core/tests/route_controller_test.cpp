#include "routes/route_controller.h"

#include <assert.h>
#include <stdio.h>

static RouteStatus tcp_result;
static int tcp_calls;
static int select_calls;

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

static void reset_stubs(RouteStatus status) {
  tcp_result = status;
  tcp_calls = 0;
  select_calls = 0;
}

int main(void) {
  PiRaceContext race = {0};
  WriteRequest request = {.mode = WRITE_MODE_ZERO};
  struct kernel_offsets values = {.compact_waiter = 1};
  TargetProfile profile = target_profile_snapshot(&values);
  RouteController controller;
  route_controller_init(
      &controller, &race, &profile, ROUTE_KIND_TCP_ZEROCOPY);

  reset_stubs((RouteStatus){
      .code = ROUTE_FALLBACK_SAFE,
      .userspace_clean = 1,
      .kernel_disarmed = 1,
  });
  RouteStatus status = route_controller_execute(&controller, &request);
  assert(status.code == ROUTE_OK);
  assert(tcp_calls == 1 && select_calls == 1 && controller.fallback_used);

  route_controller_init(
      &controller, &race, &profile, ROUTE_KIND_TCP_ZEROCOPY);
  reset_stubs((RouteStatus){
      .code = ROUTE_DIRTY_FAILURE,
      .userspace_clean = 0,
      .kernel_disarmed = 1,
  });
  status = route_controller_execute(&controller, &request);
  assert(status.code == ROUTE_DIRTY_FAILURE);
  assert(tcp_calls == 1 && select_calls == 0 && !controller.fallback_used);

  puts("route_controller_test: ok");
  return 0;
}
