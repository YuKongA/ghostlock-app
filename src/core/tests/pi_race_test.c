#include "../pi_race.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
  PiRaceContext race;
  pi_race_reset(&race, 12345, 2, 3);
  assert(race.wait_futex == 0);
  assert(race.target_futex == 0);
  assert(race.chain_futex == 0);
  assert(atomic_load(&race.waiter_ready) == 0);
  assert(atomic_load(&race.consumer_go) == 0);
  assert(atomic_load(&race.consumer_stop) == 0);
  assert(atomic_load(&race.route_delay_usec) == 12345);
  assert(race.main_cpu == 2);
  assert(race.consumer_cpu == 3);
  assert(race.waiter_started == 0);
  assert(race.owner_started_thread == 0);
  assert(race.consumer_started == 0);
  assert(race.route_status.code == ROUTE_RETRYABLE);
  puts("pi_race_test: ok");
  return 0;
}
