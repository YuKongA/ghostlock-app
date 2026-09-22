#include "../race/pi_race.h"

#include <cassert>
#include <cstdio>
#include <unistd.h>

#include <atomic>

using namespace ghostlock;


/* Stand-in workers for the host: the real entries own futex/attack logic that
 * only exists on Android, but the lifecycle contract (creation order, stop
 * signal, join order) is the same one the device gate exercises. */
static std::atomic<int32_t> g_started{0};

static bool wait_started(int32_t expected) {
    for (int32_t i = 0; i < 2000; i++) {
        if (g_started.load() >= expected) return true;
        usleep(1000);
    }
    return false;
}

static void *fake_waiter(void *arg) {
    auto *race = static_cast<ghostlock::race::PiRace *>(arg);
    g_started.fetch_add(1);
    while (!race->owner_stop.load() &&
           !race->owner_chain_done.load())
        usleep(1000);
    return nullptr;
}

static void *fake_owner(void *arg) {
    auto *race = static_cast<ghostlock::race::PiRace *>(arg);
    g_started.fetch_add(1);
    while (!race->owner_stop.load()) usleep(1000);
    return nullptr;
}

static void *fake_consumer(void *arg) {
    auto *race = static_cast<ghostlock::race::PiRace *>(arg);
    g_started.fetch_add(1);
    while (!race->consumer_stop.load()) usleep(1000);
    return nullptr;
}

int32_t main(void) {
    ghostlock::race::PiRace race;
    race.reset(12345, 2, 3);
    assert(race.wait_futex == 0);
    assert(race.target_futex == 0);
    assert(race.chain_futex == 0);
    assert(race.waiter_ready.load() == 0);
    assert(race.waiter_waiting.load() == 0);
    assert(race.owner_started.load() == 0);
    assert(race.consumer_go.load() == 0);
    assert(race.consumer_stop.load() == 0);
    assert(race.route_delay_usec.load() == 12345);
    assert(race.main_cpu == 2);
    assert(race.consumer_cpu == 3);
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(race.owner_owner.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(race.consumer_owner.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(race.request == nullptr);
    assert(race.route_status.code == ghostlock::route::ROUTE_RETRYABLE);

    /* reset is idempotent and clears previous values. */
    race.reset(1, 0, 1);
    assert(race.main_cpu == 0);
    assert(race.consumer_cpu == 1);
    assert(race.route_delay_usec.load() == 1);

    /* A rejected entry starts nothing. */
    g_started = 0;
    assert(race.start_threads(nullptr, fake_owner, fake_consumer, nullptr) ==
           EINVAL);
    assert(g_started.load() == 0);
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(race.owner_owner.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(race.consumer_owner.state() == ghostlock::support::PthreadOwner::State::Empty);

    /* Partial startup: consumer and owner start, the waiter slot is already
   * taken so its create fails and both started workers are stopped and joined
   * before the error surfaces. */
    g_started = 0;
    assert(race.waiter_owner.start(fake_waiter, &race) == 0);
    assert(race.start_threads(fake_waiter, fake_owner, fake_consumer, nullptr) ==
           EINVAL);
    assert(wait_started(3));
    assert(race.owner_owner.state() == ghostlock::support::PthreadOwner::State::Joined);
    assert(race.consumer_owner.state() == ghostlock::support::PthreadOwner::State::Joined);
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Running);
    assert(race.request == nullptr);
    race.owner_stop.store(1);
    assert(race.waiter_owner.join() == 0);
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Joined);

    /* Normal lifecycle: three workers, idempotent stop, join, repeated join. */
    g_started = 0;
    race.reset(5, 0, 1);
    assert(race.start_threads(fake_waiter, fake_owner, fake_consumer, nullptr) ==
           0);
    assert(wait_started(3));
    race.request_stop();
    race.request_stop();
    assert(race.consumer_go.load() == 0);
    assert(race.consumer_stop.load() == 1);
    assert(race.owner_stop.load() == 1);
    race.join();
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Joined);
    assert(race.owner_owner.state() == ghostlock::support::PthreadOwner::State::Joined);
    assert(race.consumer_owner.state() == ghostlock::support::PthreadOwner::State::Joined);
    race.join();
    assert(race.waiter_owner.state() == ghostlock::support::PthreadOwner::State::Joined);

    /* Outcome merge: Ok without a winning consumer call degrades to Retryable,
   * everything else is passed through untouched. */
    ghostlock::route::RouteStatus ok{};
    ok.code = ghostlock::route::ROUTE_OK;
    ok.userspace_clean = 1;
    ok.kernel_disarmed = 1;
    assert(ghostlock::race::PiRace::outcome_with_counters(ok, 3, 2).code == ghostlock::route::ROUTE_OK);
    assert(ghostlock::race::PiRace::outcome_with_counters(ok, 0, 0).code ==
           ghostlock::route::ROUTE_RETRYABLE);
    assert(ghostlock::race::PiRace::outcome_with_counters(ok, 3, 0).code ==
           ghostlock::route::ROUTE_RETRYABLE);
    assert(ghostlock::race::PiRace::outcome_with_counters(ok, 0, 2).code ==
           ghostlock::route::ROUTE_RETRYABLE);

    ghostlock::route::RouteStatus dirty{};
    dirty.code = ghostlock::route::ROUTE_DIRTY_FAILURE;
    dirty.step = 59;
    dirty.userspace_clean = 1;
    dirty.kernel_disarmed = 0;
    const ghostlock::route::RouteStatus kept = ghostlock::race::PiRace::outcome_with_counters(dirty, 10, 10);
    assert(kept.code == ghostlock::route::ROUTE_DIRTY_FAILURE);
    assert(kept.step == 59);
    assert(kept.userspace_clean == 1);
    assert(kept.kernel_disarmed == 0);

    puts("pi_race_test: ok");
    return 0;
}
