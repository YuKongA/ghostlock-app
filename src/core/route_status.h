#ifndef ROUTE_STATUS_H
#define ROUTE_STATUS_H

typedef enum RouteResultCode {
  ROUTE_OK = 0,
  ROUTE_RETRYABLE,
  ROUTE_FALLBACK_SAFE,
  ROUTE_DIRTY_FAILURE,
  ROUTE_UNSUPPORTED,
} RouteResultCode;

typedef struct RouteStatus {
  RouteResultCode code;
  /* Route-local failure checkpoint and its errno-compatible detail. */
  int step;
  int error_number;
  /* Fallback is safe only when both flags are true. */
  int userspace_clean;
  int kernel_disarmed;
} RouteStatus;

#endif
