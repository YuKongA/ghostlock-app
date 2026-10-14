#pragma once
/** @file jni/race_threads.hpp
 *  The three thread entries, parameterised from the pure-C original.
 *
 *  Source: bddfea4:src/core/main.c lines 235 (waiter), 259 (owner), 273 (consumer).
 *  Those entries take no context -- their argument is marked unused -- because they read
 *  file-scope state directly. That state is the whole subject of this file: every global
 *  they touched, and every compile-time constant they relied on, is a field here, so the
 *  caller decides instead of the translation unit.
 *
 *  Grouped the way the C file grouped them:
 *    RaceWords    the three futex words (f_pi_chain, f_wait, f_pi_target)
 *    RaceFlags    the thirteen atomic flags the three threads hand each other
 *    RaceTuning   the four constants (ROUTE_WAIT_SECONDS, PSELECT_CONSUMER_BURST_CALLS,
 *                 CONSUMER_CORE, and the literal 19 in (calls%19)+1)
 *    offsets      the one profile value they read: compact_waiter, which picks the nice
 *    RouteChoice  what tcp_route_selected() used to answer
 *
 *  Names, linkage and bodies otherwise stay as the C file had them: these three entries are
 *  already spelled the way cmp_disasm resolves them, and none of them is static.
 */
#include <cstdint>

#include "jni/route_common.hpp"
#include "jni/route_result.hpp"

namespace ghostlock::jni::race {
    /** The futex words one attempt uses. The caller owns the storage. */
    struct RaceWords final {
        std::uint32_t *wait = nullptr;
        std::uint32_t *target = nullptr;
        std::uint32_t *chain = nullptr;
    };

    /** The flags the three threads hand each other, one for one with the C globals. */
    struct RaceFlags final {
        std::int32_t waiter_tid = 0;
        std::int32_t waiter_ready = 0;
        std::int32_t waiter_waiting = 0;
        std::int32_t owner_started = 0;
        std::int32_t owner_chain_done = 0;
        std::int32_t owner_stop = 0;
        std::int32_t route_done = 0;
        std::int32_t punch_consume_stop = 0;
        std::int32_t punch_consume_go = 0;
        std::int32_t consumer_calls = 0;
        std::int32_t consumer_success = 0;
        std::int32_t consumer_inflight = 0;
        std::int32_t main_route_delay_usec = 0;
    };

    /** The four constants the C file baked in. */
    struct RaceTuning final {
        /** ROUTE_WAIT_SECONDS: added to the waiter timeout. Zero is a refusal. */
        std::uint32_t route_wait_seconds = 0U;
        /** PSELECT_CONSUMER_BURST_CALLS: how many sched_setattr calls per sequence. */
        std::uint32_t consumer_burst_calls = 0U;
        /** CONSUMER_CORE: the cpu the consumer is pinned to. */
        std::uint32_t consumer_core = 0U;
        /** The 1000 us the driver waits for waiter_waiting and owner_started. */
        std::uint32_t start_wait_usec = 0U;
        /** The 50000 us the driver waits before issuing the requeue. */
        std::uint32_t pre_requeue_usec = 0U;
        /** The 5000 us the driver waits for route_done. */
        std::uint32_t route_done_poll_usec = 0U;
        /** PSELECT_ENTER_DELAY_USEC, which the reset stores into the delay field. */
        std::uint32_t pselect_enter_delay_usec = 0U;

        /** The 19 in (calls%19)+1: the nice rotation that makes sched_setattr succeed. */
        std::uint32_t nice_period = 0U;
        /** PSELECT_CONSUMER_NICE: the nice used when the compact path does not apply. Found by
         *  reading the original ternary, which has three branches, not two. */
        std::uint32_t consumer_nice_fallback = 0U;
    };

    /** The one profile value the threads read, instead of a profile pointer. */
    struct OffsetsView final {
        bool compact_waiter = false;
        /** pselect_waiter_shift: a profile field, not a function (README.md:128). */
        std::uint32_t pselect_waiter_shift = 0U;
    };

    /** What tcp_route_selected() used to answer. */
    enum class RouteChoice : unsigned char { Pselect = 0, Tcp = 1 };

    /** Everything the three entries read, in one place the caller fills. */
    struct RaceContext final {
        RaceWords words{};
        RaceFlags flags{};
        RaceTuning tuning{};
        OffsetsView offsets{};
        RouteChoice route = RouteChoice::Pselect;
        /** The two switches the write path set through file scope (see route_common.hpp). */
        WriteModeState write_mode{};
        /** What the route wrote while the threads ran; read after join. */
        RouteResult route_result{};
    };

    /** @return NULL always; the pthread entry signature the C surface takes. */
    void *waiter_thread(void *argument);

    /** @return NULL always. */
    void *owner_thread(void *argument);

    /** @return NULL always. */
    void *consumer_thread(void *argument);
}  // namespace ghostlock::jni::race
