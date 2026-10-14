/** @file jni/race_threads.cpp
 *  The three entries, body for body from bddfea4:src/core/main.c:235-303.
 *
 *  One change, applied everywhere: the file-scope globals the C file read became fields of
 *  the RaceContext the entry receives. The argument was marked unused in the original
 *  precisely because there was no context to pass; now there is one.
 *
 *  Everything else is kept: the order of the futex operations, the usleep(1000) waits, the
 *  yield loop, the errno checks, and the sched_setattr shape with its (calls%19)+1 rotation
 *  (whose 19 is tuning.nice_period here).
 */
#include "jni/race_threads.hpp"

#include <cerrno>
#include <linux/futex.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "jni/jni_platform.hpp"
#include "support/log.hpp"

namespace ghostlock::jni::race {
    namespace {
        /** The sched_setattr shape the C file used; SCHED_BATCH(3) is the policy that walks PI. */
        struct SchedAttr final {
            std::uint32_t size = 0U;
            std::uint32_t sched_policy = 0U;
            std::uint64_t sched_flags = 0U;
            std::int32_t sched_nice = 0;
            std::uint32_t sched_priority = 0U;
            std::uint64_t sched_runtime = 0U;
            std::uint64_t sched_deadline = 0U;
            std::uint64_t sched_period = 0U;
        };

        /** One futex_op call, the platform edge this file is allowed to use. */
        int futex_op(std::uint32_t *word, int operation, int value, const struct timespec *timeout,
                     std::uint32_t *target, int target_value) noexcept {
            return static_cast<int>(::syscall(SYS_futex, word, operation, value, timeout, target,
                                              target_value));
        }

        void yield_once() noexcept { __asm__ volatile("yield" ::: "memory"); }
    }  // namespace

    void *waiter_thread(void *argument) {
        auto *const ctx = static_cast<RaceContext *>(argument);
        RaceFlags &f = ctx->flags;
        platform_disable_rseq_for_thread();
        const int tid = static_cast<int>(::syscall(SYS_gettid));
        f.waiter_tid = tid;
        if (futex_op(ctx->words.chain, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0) != 0) {
            log("warning", "waiter lock chain failed", static_cast<std::int64_t>(errno));
        }
        f.waiter_ready = 1;
        while (f.owner_started == 0) { ::usleep(1000); }
        struct timespec timeout {};
        (void) ::clock_gettime(CLOCK_MONOTONIC, &timeout);
        timeout.tv_sec += static_cast<time_t>(ctx->tuning.route_wait_seconds);
        f.waiter_waiting = 1;
        (void) futex_op(ctx->words.wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, ctx->words.target, 0);
        if (ctx->route == RouteChoice::Tcp) {
            do_tcp_fake_lock_route(ctx->route_result);
        } else {
            do_pselect_fake_lock_route(ctx->route_result);
        }
        f.route_done = 1;
        (void) futex_op(ctx->words.chain, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        while (f.owner_chain_done == 0) { ::usleep(1000); }
        return nullptr;
    }

    void *owner_thread(void *argument) {
        auto *const ctx = static_cast<RaceContext *>(argument);
        RaceFlags &f = ctx->flags;
        platform_disable_rseq_for_thread();
        const long lock_target = futex_op(ctx->words.target, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        if (lock_target != 0) {
            log("warning", "owner lock target failed", static_cast<std::int64_t>(errno));
        }
        while (f.waiter_ready == 0) { ::usleep(1000); }
        f.owner_started = 1;
        (void) futex_op(ctx->words.chain, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0);
        f.owner_chain_done = 1;
        while (f.owner_stop == 0) { ::sleep(1); }
        if (lock_target == 0) {
            (void) futex_op(ctx->words.target, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0);
        }
        return nullptr;
    }

    void *consumer_thread(void *argument) {
        auto *const ctx = static_cast<RaceContext *>(argument);
        RaceFlags &f = ctx->flags;
        platform_disable_rseq_for_thread();
        platform_pin_to_core(ctx->tuning.consumer_core);
        log("info", "consumer thread running", static_cast<std::int64_t>(::sched_getcpu()));
        std::int32_t seen = 0;
        while (f.punch_consume_stop == 0) {
            const std::int32_t seq = f.punch_consume_go;
            if (seq == 0 || seq == seen) {
                yield_once();
                continue;
            }
            seen = seq;
            const std::int32_t tid = f.waiter_tid;
            std::int32_t calls_this_seq = 0;
            while (f.punch_consume_stop == 0 && f.punch_consume_go == seq) {
                const std::int32_t delay_usec = f.main_route_delay_usec;
                if (delay_usec > 0) { ::usleep(static_cast<useconds_t>(delay_usec)); }
                for (std::uint32_t burst = 0U; burst < ctx->tuning.consumer_burst_calls; burst++) {
                    if (f.punch_consume_stop != 0 || f.punch_consume_go != seq) { break; }
                    f.consumer_calls = f.consumer_calls + 1;
                    f.consumer_inflight = 1;
                    errno = 0;
                    /* rotate the nice every call; (calls%19)+1 is what makes sched_setattr
                     *  succeed on 6.1 compact. The 19 is tuning.nice_period now, and the
                     *  original had three branches, not two: the fallback is its own
                     *  constant PSELECT_CONSUMER_NICE, which is a field here. */
                    const std::int32_t period = static_cast<std::int32_t>(ctx->tuning.nice_period);
                    const std::int32_t consumer_nice =
                        ctx->offsets.compact_waiter
                            ? (calls_this_seq % period) + 1
                            : static_cast<std::int32_t>(ctx->tuning.consumer_nice_fallback);
                    SchedAttr attr{};
                    attr.size = sizeof(SchedAttr);
                    attr.sched_policy = 3U; /* SCHED_BATCH */
                    attr.sched_nice = consumer_nice;
                    const long ret =
                        static_cast<long>(::syscall(SYS_sched_setattr, tid, &attr, 0U));
                    if (ret == 0) { f.consumer_success = f.consumer_success + 1; }
                    ++calls_this_seq;
                    f.consumer_inflight = 0;
                }
            }
        }
        return nullptr;
    }
}  // namespace ghostlock::jni::race
