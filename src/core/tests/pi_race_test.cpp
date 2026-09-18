#include "../pi_race.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <atomic>

using ghostlock::PthreadOwner;

/* Stand-in workers for the host: the real entries own futex/attack logic that
 * only exists on Android, but the lifecycle contract (creation order, stop
 * signal, join order) is the same one the device gate exercises. */
static std::atomic<int> g_started{0};

static bool wait_started(int expected) {
  for (int i = 0; i < 2000; i++) {
    if (g_started.load() >= expected) return true;
    usleep(1000);
  }
  return false;
}

static void *fake_waiter(void *arg) {
  auto *race = static_cast<PiRaceContext *>(arg);
  g_started.fetch_add(1);
  while (!atomic_load(&race->owner_stop) &&
          !atomic_load(&race->owner_chain_done))
    usleep(1000);
  return nullptr;
}

static void *fake_owner(void *arg) {
  auto *race = static_cast<PiRaceContext *>(arg);
  g_started.fetch_add(1);
  while (!atomic_load(&race->owner_stop)) usleep(1000);
  return nullptr;
}

static void *fake_consumer(void *arg) {
  auto *race = static_cast<PiRaceContext *>(arg);
  g_started.fetch_add(1);
  while (!atomic_load(&race->consumer_stop)) usleep(1000);
  return nullptr;
}

int main(void) {
  PiRaceContext race;
  race.reset(12345, 2, 3);
  assert(race.wait_futex == 0);
  assert(race.target_futex == 0);
  assert(race.chain_futex == 0);
  assert(atomic_load(&race.waiter_ready) == 0);
  assert(atomic_load(&race.waiter_waiting) == 0);
  assert(atomic_load(&race.owner_started) == 0);
  assert(atomic_load(&race.consumer_go) == 0);
  assert(atomic_load(&race.consumer_stop) == 0);
  assert(atomic_load(&race.route_delay_usec) == 12345);
  assert(race.main_cpu == 2);
  assert(race.consumer_cpu == 3);
  assert(race.waiter_owner.state() == PthreadOwner::State::Empty);
  assert(race.owner_owner.state() == PthreadOwner::State::Empty);
  assert(race.consumer_owner.state() == PthreadOwner::State::Empty);
  assert(race.request == nullptr);
  assert(race.route_status.code == ROUTE_RETRYABLE);

  /* reset is idempotent and clears previous values. */
  race.reset(1, 0, 1);
  assert(race.main_cpu == 0);
  assert(race.consumer_cpu == 1);
  assert(atomic_load(&race.route_delay_usec) == 1);

  /* A rejected entry starts nothing. */
  g_started = 0;
  assert(race.start_threads(nullptr, fake_owner, fake_consumer, nullptr) ==
         EINVAL);
  assert(g_started.load() == 0);
  assert(race.waiter_owner.state() == PthreadOwner::State::Empty);
  assert(race.owner_owner.state() == PthreadOwner::State::Empty);
  assert(race.consumer_owner.state() == PthreadOwner::State::Empty);

  /* Partial startup: consumer and owner start, the waiter slot is already
   * taken so its create fails and both started workers are stopped and joined
   * before the error surfaces. */
  g_started = 0;
  assert(race.waiter_owner.start(fake_waiter, &race) == 0);
  assert(race.start_threads(fake_waiter, fake_owner, fake_consumer, nullptr) ==
         EINVAL);
  assert(wait_started(3));
  assert(race.owner_owner.state() == PthreadOwner::State::Joined);
  assert(race.consumer_owner.state() == PthreadOwner::State::Joined);
  assert(race.waiter_owner.state() == PthreadOwner::State::Running);
  assert(race.request == nullptr);
  atomic_store(&race.owner_stop, 1);
  assert(race.waiter_owner.join() == 0);
  assert(race.waiter_owner.state() == PthreadOwner::State::Joined);

  /* Normal lifecycle: three workers, idempotent stop, join, repeated join. */
  g_started = 0;
  race.reset(5, 0, 1);
  assert(race.start_threads(fake_waiter, fake_owner, fake_consumer, nullptr) ==
         0);
  assert(wait_started(3));
  race.request_stop();
  race.request_stop();
  assert(atomic_load(&race.consumer_go) == 0);
  assert(atomic_load(&race.consumer_stop) == 1);
  assert(atomic_load(&race.owner_stop) == 1);
  race.join();
  assert(race.waiter_owner.state() == PthreadOwner::State::Joined);
  assert(race.owner_owner.state() == PthreadOwner::State::Joined);
  assert(race.consumer_owner.state() == PthreadOwner::State::Joined);
  race.join();
  assert(race.waiter_owner.state() == PthreadOwner::State::Joined);

  /* Outcome merge: Ok without a winning consumer call degrades to Retryable,
   * everything else is passed through untouched. */
  RouteStatus ok{};
  ok.code = ROUTE_OK;
  ok.userspace_clean = 1;
  ok.kernel_disarmed = 1;
  assert(PiRaceContext::outcome_with_counters(ok, 3, 2).code == ROUTE_OK);
  assert(PiRaceContext::outcome_with_counters(ok, 0, 0).code ==
         ROUTE_RETRYABLE);
  assert(PiRaceContext::outcome_with_counters(ok, 3, 0).code ==
         ROUTE_RETRYABLE);
  assert(PiRaceContext::outcome_with_counters(ok, 0, 2).code ==
         ROUTE_RETRYABLE);

  RouteStatus dirty{};
  dirty.code = ROUTE_DIRTY_FAILURE;
  dirty.step = 59;
  dirty.userspace_clean = 1;
  dirty.kernel_disarmed = 0;
  const RouteStatus kept = PiRaceContext::outcome_with_counters(dirty, 10, 10);
  assert(kept.code == ROUTE_DIRTY_FAILURE);
  assert(kept.step == 59);
  assert(kept.userspace_clean == 1);
  assert(kept.kernel_disarmed == 0);

  puts("pi_race_test: ok");
  return 0;
}
