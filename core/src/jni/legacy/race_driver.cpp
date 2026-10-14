/** @file jni/race_driver.cpp
 *  The driver body, from bddfea4:src/core/main.c:337-359, with the reset at :331-334.
 *
 *  Every global the C file touched is a field of the context passed in; every literal wait is
 *  a tuning field. The two structural differences from the C++ tree -- no race object, and the
 *  trigger living here rather than in the waiter -- are stated in the header.
 */
#include "jni/race_driver.hpp"

#include <cerrno>
#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "support/log.hpp"

namespace ghostlock::jni {
    namespace {
        /** Named refusals, so a caller can tell which parameter was missing. */
        constexpr std::int32_t kDriverBadWords = 1;
        constexpr std::int32_t kDriverBadTuning = 2;
        constexpr std::int32_t kDriverThreadStart = 3;

        /** One futex_op call: the platform edge this file is allowed to use. */
        int futex_op(std::uint32_t *word, int operation, std::uint32_t value, void *timeout,
                     std::uint32_t *target, std::uint32_t target_value) noexcept {
            return static_cast<int>(::syscall(SYS_futex, word, operation, value, timeout, target,
                                              target_value));
        }

        /** The reset the C file did at :331-334, field by field. */
        void reset_main_route_state(race::RaceContext &ctx) noexcept {
            race::RaceFlags &f = ctx.flags;
            f.consumer_calls = 0;
            f.consumer_success = 0;
            f.consumer_inflight = 0;
            f.main_route_delay_usec = static_cast<std::int32_t>(ctx.tuning.pselect_enter_delay_usec);
            ctx.route_result.step = 0;
            ctx.route_result.error_number = 0;
        }
    }  // namespace

    bool run_main_route_threads(race::RaceContext &context) noexcept {
        /* Parameterised guard: the C file read these from file scope and could not tell
         *  absent from zero; with values arriving from the caller it must. */
        if (context.words.wait == nullptr || context.words.target == nullptr ||
            context.words.chain == nullptr) {
            context.route_result.step = kDriverBadWords;
            log("error", "driver: a futex word is absent");
            return false;
        }
        if (context.tuning.start_wait_usec == 0U || context.tuning.pre_requeue_usec == 0U ||
            context.tuning.route_done_poll_usec == 0U ||
            context.tuning.pselect_enter_delay_usec == 0U) {
            context.route_result.step = kDriverBadTuning;
            log("error", "driver: a wait value is absent");
            return false;
        }
        reset_main_route_state(context);

        pthread_t waiter{};
        pthread_t owner{};
        pthread_t consumer{};
        /* Three independent creations, as the C file had them: a failure in one must not
         *  skip the others, and whatever was created must be joined before returning. */
        const int w_err = ::pthread_create(&waiter, nullptr, &race::waiter_thread, &context);
        const int o_err = w_err == 0 ? ::pthread_create(&owner, nullptr, &race::owner_thread, &context) : -1;
        const int c_err =
            (w_err == 0 && o_err == 0) ? ::pthread_create(&consumer, nullptr, &race::consumer_thread, &context) : -1;
        if (w_err != 0 || o_err != 0 || c_err != 0) {
            context.flags.punch_consume_stop = 1;
            context.flags.owner_stop = 1;
            if (w_err == 0) { (void) ::pthread_join(waiter, nullptr); }
            if (o_err == 0) { (void) ::pthread_join(owner, nullptr); }
            if (c_err == 0) { (void) ::pthread_join(consumer, nullptr); }
            context.route_result.step = kDriverThreadStart;
            log("warning", "driver: PI race thread creation failed",
                static_cast<std::int64_t>(errno));
            return false;
        }
        while (context.flags.waiter_waiting == 0 || context.flags.owner_started == 0) {
            ::usleep(context.tuning.start_wait_usec);
        }
        ::usleep(context.tuning.pre_requeue_usec);
        errno = 0;
        /* The trigger: move the parked waiter onto the target word. This is the step the C++
         *  tree moved inside the waiter; here it stays where the C file had it. */
        (void) futex_op(context.words.wait, FUTEX_CMP_REQUEUE_PI, 1U, reinterpret_cast<void *>(1),
                        context.words.target, 0U);
        while (context.flags.route_done == 0) {
            ::usleep(context.tuning.route_done_poll_usec);
        }

        context.flags.punch_consume_go = 0;
        context.flags.punch_consume_stop = 1;
        context.flags.owner_stop = 1;
        (void) ::pthread_join(waiter, nullptr);
        (void) ::pthread_join(owner, nullptr);
        (void) ::pthread_join(consumer, nullptr);

        /* The three conditions the C file returned. */
        const bool won = context.flags.consumer_calls > 0 && context.flags.consumer_success > 0 &&
                         context.route_result.step == 0;
        context.route_result.code = won ? 0 : 1;
        log("info", "driver: race finished", static_cast<std::int64_t>(context.flags.consumer_success));
        return won;
    }
}  // namespace ghostlock::jni
