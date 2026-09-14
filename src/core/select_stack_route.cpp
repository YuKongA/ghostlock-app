#include "select_stack_route.h"

#include <string.h>

void select_stack_route_context_init(
    SelectStackRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    SelectStackLayout layout,
    const int stdio_backup[3]) {
  memset(context, 0, sizeof(*context));
  context->race = race;
  context->request = request;
  context->execution = execution;
  context->layout = layout;
  context->pipe_fd[0] = -1;
  context->pipe_fd[1] = -1;
  context->block_fd = -1;
  context->high_read_fd = -1;
  for (int i = 0; i < 3; i++) {
    context->stdio_backup[i] = stdio_backup ? stdio_backup[i] : -1;
  }
  context->status.code = ROUTE_RETRYABLE;
}
