#ifndef GHOSTLOCK_KERNEL_CONSTANTS_HPP
#define GHOSTLOCK_KERNEL_CONSTANTS_HPP

#include <cstdint>

#include "kernel/offset.h"

namespace ghostlock::kernel {
    inline constexpr unsigned PAGE_SHIFT = 12;
    inline constexpr unsigned long PAGE_SIZE = 1UL << PAGE_SHIFT;
    inline constexpr unsigned KS_PAGE_SIZE = 4096;
    inline constexpr unsigned long long KS_PAGE_MASK = 0xfffULL;

    inline constexpr long long SKB_DATA_DELTA = -0xe80LL;
    inline constexpr unsigned long MM_STRUCT_SZ = 0x500;

    inline constexpr unsigned MM_ORDER = 3;
    inline constexpr unsigned MM_PARTIALS = 5;

    inline constexpr unsigned long ORDER3_SIZE = PAGE_SIZE << MM_ORDER;
    inline constexpr unsigned long SKB_SEND_SIZE = ORDER3_SIZE * 2;
    inline constexpr unsigned SKB_RECLAIM_SENDS = 4;
    inline constexpr unsigned long FOPS_TABLE_OFF = FOPS_OFF;
    inline constexpr int32_t SKB_FRAG_BIAS = 0;

    inline constexpr int32_t FAKE_TASK_PRIO = 120;
    inline constexpr int32_t FAKE_WAITER_PRIO = 140;
    inline constexpr unsigned FAKE_TASK_UCLAMP_REQ_OFF = 0x350;
    inline constexpr unsigned FAKE_TASK_UCLAMP_OFF = 0x358;
    inline constexpr unsigned FAKE_UCLAMP_ACTIVE_BIT = 16;
    inline constexpr unsigned FAKE_UCLAMP_MIN_ACTIVE = 1U << FAKE_UCLAMP_ACTIVE_BIT;
    inline constexpr unsigned FAKE_UCLAMP_MAX_ACTIVE =
            (1024U | (19U << 11) | (1U << FAKE_UCLAMP_ACTIVE_BIT));

    inline constexpr unsigned TASK_COMM_LEN = 16;

    /* `select_stack_route.h` keeps a host-safe fallback macro under this name, so a
 * TU that includes it before common.h must not also declare the constexpr. */
#ifndef PSELECT_ROUTE_NFDS
    inline constexpr unsigned PSELECT_ROUTE_NFDS = 320;
#endif
    inline constexpr int32_t PSELECT_CONSUMER_NICE = 19;
    inline constexpr unsigned PSELECT_CONSUMER_SETTLE_USEC = 250000;

    struct local_sched_attr {
        uint32_t size;
        uint32_t sched_policy;
        uint64_t sched_flags;
        int32_t sched_nice;
        uint32_t sched_priority;
        uint64_t sched_runtime;
        uint64_t sched_deadline;
        uint64_t sched_period;
    };

    /* Measured direct-map end (defaults to the built-in bound). */
    extern uint64_t g_direct_map_end;
} // namespace ghostlock::kernel

#endif
