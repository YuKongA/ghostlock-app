#include "routes/select_stack_route.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
  SelectStackRouteContext context;
  PiRaceContext race = {0};
  WriteRequest request = {0};
  SelectStackLayout layout = {.waiter_shift = 16, .compact_waiter = 1};
  int backups[3] = {100, 101, 102};
  const struct execution_settings *execution =
      (const struct execution_settings *)(uintptr_t)0x1234;

  select_stack_route_context_init(
      &context, &race, &request, execution, layout, backups);
  assert(context.race == &race && context.request == &request);
  assert(context.execution == execution);
  assert(context.layout.waiter_shift == 16 && context.layout.compact_waiter);
  assert(context.pipe_fd[0] == -1 && context.pipe_fd[1] == -1);
  assert(context.block_fd == -1 && context.high_read_fd == -1);
  assert(context.stdio_backup[0] == 100 && context.stdio_backup[2] == 102);
  assert(context.status.code == ROUTE_RETRYABLE);
  puts("select_stack_route_test: ok");
  return 0;
}
