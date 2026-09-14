#include "routes/tcp_zerocopy_route.h"

#include <sys/mman.h>

void tcp_zerocopy_route_context_init(
        TcpZerocopyRouteContext *context,
        PiRaceContext *race,
        const WriteRequest *request,
        const struct execution_settings *execution,
        size_t mapping_length) {
    context->race = race;
    context->request = request;
    context->execution = execution;
    context->client_fd = -1;
    context->server_fd = -1;
    context->punch_fd = -1;
    context->mapping = static_cast<unsigned char *>(MAP_FAILED);
    context->mapping_length = mapping_length;
    context->page_size = 0;
    context->punch_worker = {};
    context->punch_worker_started = 0;
    context->route_won = 0;
    context->status = {};
    context->status.code = ROUTE_RETRYABLE;
    atomic_store_explicit(&context->punch_go, 0, memory_order_relaxed);
    atomic_store_explicit(&context->punch_stop, 0, memory_order_relaxed);
    atomic_store_explicit(&context->punch_phase, 0, memory_order_relaxed);
    atomic_store_explicit(&context->punch_failed, 0, memory_order_relaxed);
}
