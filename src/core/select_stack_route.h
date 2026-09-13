#ifndef SELECT_STACK_ROUTE_H
#define SELECT_STACK_ROUTE_H

#include "payload_builder.h"
#include "pi_race.h"
#include "profile.h"
#include "route_status.h"

#include <sys/select.h>

typedef struct SelectStackRouteContext {
  PiRaceContext *race;
  const WriteRequest *request;
  const struct execution_settings *execution;
  SelectStackLayout layout;
  fd_set input_set;
  fd_set output_set;
  fd_set exception_set;
  fd_set owned_input_set;
  fd_set owned_output_set;
  fd_set owned_exception_set;
  int pipe_fd[2];
  int block_fd;
  int high_read_fd;
  int stdio_backup[3]; /* borrowed from the process logging lifetime */
  int selected_fds_installed;
  int consumer_stuck;
  int calls;
  int successes;
  int select_result;
  int select_errno;
  RouteStatus status;
} SelectStackRouteContext;

void select_stack_route_context_init(
    SelectStackRouteContext *context,
    PiRaceContext *race,
    const WriteRequest *request,
    const struct execution_settings *execution,
    SelectStackLayout layout,
    const int stdio_backup[3]);

#endif
