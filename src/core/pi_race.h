#ifndef PI_RACE_H
#define PI_RACE_H

#include "payload_builder.h"
#include "route_status.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct PiRaceContext {
  uint32_t wait_futex;
  uint32_t target_futex;
  uint32_t chain_futex;
  atomic_int waiter_ready;
  atomic_int waiter_waiting;
  atomic_int owner_started;
  atomic_int owner_chain_done;
  atomic_int owner_stop;
  atomic_int route_done;
  atomic_int waiter_tid;
  atomic_int consumer_go;
  atomic_int consumer_stop;
  atomic_int consumer_calls;
  atomic_int consumer_success;
  atomic_int consumer_inflight;
  atomic_int route_delay_usec;
  atomic_int fast_repair;
  int main_cpu;
  int consumer_cpu;
  pthread_t waiter_thread;
  pthread_t owner_thread;
  pthread_t consumer_thread;
  int waiter_started;
  int owner_started_thread;
  int consumer_started;
  const WriteRequest *request;
  RouteStatus route_status;
} PiRaceContext;

#ifdef __cplusplus
extern PiRaceContext &g_pi_race_context;
#else
extern PiRaceContext g_pi_race_context;
#endif

void pi_race_reset(
    PiRaceContext *context, int initial_delay_usec,
    int main_cpu, int consumer_cpu);
int pi_race_start(PiRaceContext *context, const WriteRequest *request);
int pi_race_run(PiRaceContext *context);
void pi_race_stop(PiRaceContext *context);
void pi_race_destroy(PiRaceContext *context);

#endif
