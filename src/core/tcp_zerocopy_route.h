#ifndef TCP_ZEROCOPY_ROUTE_H
#define TCP_ZEROCOPY_ROUTE_H

#include "payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "route_status.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

typedef struct TcpZerocopyRouteContext {
  PiRaceContext *race;
  const WriteRequest *request;
  const struct execution_settings *execution;
  int client_fd;
  int server_fd;
  int punch_fd;
  unsigned char *mapping;
  size_t mapping_length;
  size_t page_size;
  pthread_t punch_worker;
  int punch_worker_started;
  atomic_int punch_go;
  atomic_int punch_stop;
  atomic_int punch_phase;
  atomic_int punch_failed;
  int route_won;
  RouteStatus status;
} TcpZerocopyRouteContext;

void tcp_zerocopy_route_context_init(
    TcpZerocopyRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    size_t mapping_length);

#endif
