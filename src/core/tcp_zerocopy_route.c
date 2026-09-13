#include "tcp_zerocopy_route.h"

#include <string.h>
#include <sys/mman.h>

void tcp_zerocopy_route_context_init(
    TcpZerocopyRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    size_t mapping_length) {
  memset(context, 0, sizeof(*context));
  context->race = race;
  context->request = request;
  context->execution = execution;
  context->client_fd = -1;
  context->server_fd = -1;
  context->punch_fd = -1;
  context->mapping = MAP_FAILED;
  context->mapping_length = mapping_length;
  context->status.code = ROUTE_RETRYABLE;
  atomic_init(&context->punch_go, 0);
  atomic_init(&context->punch_stop, 0);
  atomic_init(&context->punch_phase, 0);
  atomic_init(&context->punch_failed, 0);
}
