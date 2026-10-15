#pragma once
/** @file jni/pselect_route.hpp
 *  The pselect route, parameterised. Source: bddfea4:src/core/fops.c:495-663 (169 lines).
 *
 *  What the C body reached through file scope now arrives as fields: the three addresses it
 *  guards on, the spray products it reads, its own tuning, the offsets, the two write-path
 *  switches, and the flags it shares with the three threads. The fds and syscalls stay here.
 *
 *  Two things must survive the move, because both look like defects and neither is:
 *    1 the route may replace low fds including stdout and stderr, so it reserves and restores
 *       standard io around the select (original comment: "The route may replace low fds,"
 *       "including stdout and stderr.");
 *    2 when the consumer is still inflight the route leaks its fds on purpose: closing them
 *       would reclaim objects the consumer is still using (original comment: "stuck in
 *       sched_setattr or futex, closing would reclaim objects its syscall still uses, leak and
 *       let process exit reclaim them"). The step recorded is 34 and the pipe is left open.
 */
#include <cstdint>

#include "jni/race_threads.hpp"
#include "jni/route_common.hpp"
#include "jni/route_landings.hpp"

namespace ghostlock::jni {
    /** Everything one pselect attempt reads, in one place the caller fills. */
    struct PselectRouteInputs final {
        /** The three addresses the guard checks; any null is a refusal. */
        RouteAddrs addrs{};
        /** What the spray produced; the route reads fake_task and fake_w0 for its log. */
        const SprayProducts *products = nullptr;
        /** The offsets the fdsets and the W1 branch read. */
        const SprayOffsets *offsets = nullptr;
        /** The tuning of this route: attempts, both timeouts, the nfds, and the two drain values. */
        const PselectTuning *tuning = nullptr;
        /** The flags shared with the three threads, and the words. */
        race::RaceContext *race = nullptr;
        /** The helpers that must run C side. */
        const RouteSeams *ops = nullptr;
        /** The waiter shift, a profile field. */
        std::uint32_t waiter_shift = 0U;
    };

    /**
     *  Run one pselect attempt set.
     *
     *  @param inputs  everything the body reads; nothing is fetched from file scope.
     *  @param result  receives the step and errno the C body reported, and whether it won.
     *  @note Winning is judged as the C body judged it: the consumer must have been called and
     *        successful at least once (calls > 0 && success > 0).
     */
    void do_kernel5_fake_lock_route(const PselectRouteInputs &inputs, RouteResult &result) noexcept;
}  // namespace ghostlock::jni
