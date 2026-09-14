#include "routes/tcp_zerocopy_route.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

int main(void) {
  TcpZerocopyRouteContext context;
  PiRaceContext race = {0};
  WriteRequest request = {0};
  const struct execution_settings *execution =
      (const struct execution_settings *)(uintptr_t)0x1234;

  tcp_zerocopy_route_context_init(
      &context, &race, &request, execution, 16 * 1024 * 1024);

  assert(context.race == &race);
  assert(context.request == &request);
  assert(context.execution == execution);
  assert(context.client_fd == -1);
  assert(context.server_fd == -1);
  assert(context.punch_fd == -1);
  assert(context.mapping == MAP_FAILED);
  assert(context.mapping_length == 16 * 1024 * 1024);
  assert(context.punch_worker_started == 0);
  assert(atomic_load(&context.punch_go) == 0);
  assert(atomic_load(&context.punch_stop) == 0);
  assert(atomic_load(&context.punch_phase) == 0);
  assert(atomic_load(&context.punch_failed) == 0);
  assert(context.status.code == ROUTE_RETRYABLE);
  assert(context.status.userspace_clean == 0);
  assert(context.status.kernel_disarmed == 0);

  puts("tcp_zerocopy_route_test: ok");
  return 0;
}
