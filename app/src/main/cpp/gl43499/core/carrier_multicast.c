/**
 * @file carrier_multicast.c
 * @brief 43499 Multicast Carrier 的物理实现（`MCAST_BLOCK_SOURCE` 单发 stamp）。
 *
 * @note 移植自 2.0.8 `route_multicast.cpp:MulticastRoute::prepare/execute/disarm/
 *       destroy` 的物理 syscall 序列，改写为 C11；不 include 也不链接
 *       `legacy` 各子目录。旧 route 状态机、policy 钩子与 fallback 均不引入。
 * @note 语义边界：stamp 字节由 Kotlin 提供（`frame_bytes`，已含 waiter/task/lock
 *       与 family 的最终布局）；Native 只校验句柄/长度/上限并原样发送，不构造
 *       payload、不解释业务含义、不换 Primitive。
 * @note PI 窗口：触发入口由 Actor 在 prepare 阶段绑定；函数内只做 setsockopt、
 *       内联原子与 yield，不 kind 读取、不分配、不加锁、不写日志、不首次触页
 *       （frame 已在 prepare 深拷贝并触页）。
 * @note 阶段：G4L-4M。
 */

#include "carrier_internal.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief 校验 Multicast 的最终地址三元组。
 * @param addresses Kotlin 给出的最终地址（Native 不再从 offset 重算）。
 * @return 1 接受；0 拒绝（物理上必须三个地址都非零）。
 */
static int32_t glk43499_multicast_valid_addresses(
    const struct glk43499_primitive_addresses *addresses) {
    return addresses->final_waiter_address != 0 &&
           addresses->final_task_address != 0 &&
           addresses->final_lock_address != 0;
}

int32_t glk43499_carrier_multicast_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_multicast_spec *spec) {
    struct glk43499_carrier_multicast *multicast =
        &carrier->value.multicast_sendmsg;
    memset(multicast, 0, sizeof(*multicast));
    multicast->socket_fd = -1;
    if (spec == NULL || spec->frame_bytes == NULL) return -EINVAL;
    if (!glk43499_multicast_valid_addresses(&spec->addresses)) return -EINVAL;
    /* A 决策（R2b）：帧容量上限来自请求字段 max_frame（Native 无容量宏）。 */
    if (spec->frame_size == 0 || spec->max_frame == 0 ||
        spec->frame_size > spec->max_frame) {
        return -EINVAL;
    }
    if (spec->address_family != 0 && spec->address_family != AF_INET) {
        return -EINVAL;
    }
    /* B15：attempt 计数最终收窄到 int32（consumer_go/回执），域上限显式。 */
    if (spec->operation_budget == 0 ||
        spec->operation_budget > (uint64_t) INT32_MAX) {
        return -EINVAL;
    }
    /* 超时非零且 deadline 加法不溢出（不内置 1 小时目标上限）。 */
    const uint64_t now_ns = glk43499_internal_now_ns();
    if (spec->timeout_ns == 0 ||
        spec->timeout_ns > UINT64_MAX - now_ns) {
        return -EINVAL;
    }
    /* socket 缓冲：setsockopt 体系上限由系统头派生；非零且可表示。 */
    if (spec->socket_buffer_size == 0 ||
        spec->socket_buffer_size > (uint64_t) INT32_MAX) {
        return -EINVAL;
    }
    /* P0a：arm 序列/保持参数来自请求；非法值 fail-closed。 */
    if (spec->arm_sequence == 0 || spec->arm_hold_ns == 0) {
        return -EINVAL;
    }
    /* D3/B7：ghost disarm 参数域校验；disarm_required=0 表示不做 disarm（同 legacy
     * FAILURE→不做）。required=1 时 futex_waiter_flag 必须非零 —— 只带 ghost_pid
     * 的 FUTEX_LOCK_PI 不是旧 D3 disarm 的等价物（审计 B7：旧实现允许 flag=0）。 */
    if (spec->ghost_pid <= 0 ||
        !glk43499_internal_valid_futex_waiter_flag(
            spec->futex_waiter_flag) ||
        spec->disarm_required > 1 ||
        (spec->disarm_required != 0 && spec->futex_waiter_flag == 0)) {
        return -EINVAL;
    }
    multicast->arm_hold_ns = spec->arm_hold_ns;
    multicast->arm_sequence = spec->arm_sequence;
    multicast->ghost_pid = spec->ghost_pid;
    multicast->futex_waiter_flag = spec->futex_waiter_flag;
    multicast->disarm_required = spec->disarm_required;
    /* 深拷贝 frame：JNI 的临时缓冲在 land/prepare 返回后即释放。 */
    multicast->frame = (uint8_t *) malloc((size_t) spec->frame_size);
    if (multicast->frame == NULL) return -ENOMEM;
    memcpy(multicast->frame, spec->frame_bytes, (size_t) spec->frame_size);
    multicast->frame_size = spec->frame_size;
    multicast->address_family = spec->address_family;
    multicast->operation_budget = spec->operation_budget;
    multicast->timeout_ns = spec->timeout_ns;
    multicast->socket_fd =
        socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (multicast->socket_fd < 0) {
        const int32_t saved = errno;
        free(multicast->frame);
        multicast->frame = NULL;
        multicast->frame_size = 0;
        return -saved;
    }
    multicast->prepared = 1;
    return 0;
}

/**
 * @brief PI 窗口内执行 Multicast 触发：重复 stamp + 驱动 consumer 握手。
 * @param carrier 已 prepare 的 Carrier；只访问 multicast 分支。
 * @param shared PI 共享状态。
 * @note 触发条件沿用旧物理判据：`setsockopt` 返回 0 或 `EADDRNOTAVAIL`
 *       （copy 已落在内核栈上）。arm 后以 consumer 首个调用为界，成功即退出。
 * @note 窗口内禁止分配/日志/锁；本函数只调用 syscall 与内联原子。
 */
void glk43499_carrier_multicast_trigger(
    struct glk43499_carrier *carrier, struct glk43499_pi_shared *shared,
    struct glk43499_carrier_outcome *outcome) {
    struct glk43499_carrier_multicast *multicast =
        &carrier->value.multicast_sendmsg;
    int32_t result = 0;
    /* task-113：崩溃前快照（只写 atomic；无 IO/日志）。 */
    atomic_store(&shared->carrier_started, 1);
    atomic_store(&shared->route_enter_ns,
                 (uint64_t) atomic_load(&shared->requeue_call_ns) + 1u);
    atomic_store(&shared->consumer_calls, 0);
    atomic_store(&shared->consumer_success, 0);
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&shared->route_delay_usec, 0);

    uint64_t operations = 0;
    int32_t last_stamp_result = -1;
    int32_t last_stamp_errno = 0;
    int32_t inflight_timeout = 0;
    /* R3/B2：与 legacy2 `route_multicast.cpp:101-131` 对齐的 arm 语义 ——
     * operation_budget 是总 stamp attempt 上限（旧 attempts=128）；
     * attempt < arm_sequence 前只重喷不 arm（旧 arm_sequence=16）；
     * arm 后按 arm_hold_ns 的**自旋次数**等待 consumer 首调用（旧 arm_hold=20000）。
     * B12：窗口内不取墙钟（原 deadline/vDSO 依赖已删除）；
     * 总时长由 operation_budget 与自旋次数共同界定，不依赖 vDSO。 */
    for (uint64_t attempt = 1; attempt <= multicast->operation_budget;
         ++attempt) {
        ++operations;
        errno = 0;
        const int32_t stamp_result =
            setsockopt(multicast->socket_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                       multicast->frame, (socklen_t) multicast->frame_size);
        last_stamp_result = stamp_result;
        last_stamp_errno = errno;
        /* Step C：append-only carrier 快照（无 IO/日志）。 */
        atomic_store(&shared->carrier_frames_sent, (int32_t) operations);
        atomic_store(&shared->carrier_last_errno, last_stamp_errno);
        /* copy 先于 family 检查完成，因此 EADDRNOTAVAIL 仍表示 waiter 已上栈。 */
        if (attempt < multicast->arm_sequence ||
            (stamp_result != 0 && last_stamp_errno != EADDRNOTAVAIL)) {
            continue;
        }
        atomic_store(&shared->consumer_go, (int32_t) attempt);
        for (uint64_t spin = 0;
             spin < multicast->arm_hold_ns &&
             atomic_load(&shared->consumer_calls) == 0;
             ++spin) {
            __asm__ volatile("yield" ::: "memory");
        }
        atomic_store(&shared->consumer_go, 0);
        /* Step B1（Lead 批准 semantic change；legacy 依据 legacy2/core/route_multicast.cpp:126-127）：
         * 改回**无界**等待 consumer 退出 inflight（legacy 同形）；B13 有界化会提前收尾窗口，
         * 使 consumer 仍 walk 被 poison 的内核栈帧 ⇒ UAF/panic/hang 候选。 */
        while (atomic_load(&shared->consumer_inflight) != 0) {
            __asm__ volatile("yield" ::: "memory");
        }
        if (atomic_load(&shared->consumer_success) > 0) {
            result = 1;
            break;
        }
    }
    /* legacy 成功判据：stamp 成功返回 0 或 consumer 观察到成功。
     * B13：inflight 超限必须保持失败（不得被 legacy 成功回退覆盖）。 */
    if (!inflight_timeout && last_stamp_result == 0 &&
        atomic_load(&shared->consumer_success) == 0) {
        result = 1;
    }
    atomic_store(&shared->carrier_step,
                 inflight_timeout != 0 ? GLK43499_CARRIER_STEP_INFLIGHT
                                       : GLK43499_CARRIER_STEP_IO);
    if (outcome != NULL) {
        outcome->operations = operations;
        outcome->calls = (uint64_t) atomic_load(&shared->consumer_calls);
        outcome->successes = (uint64_t) atomic_load(&shared->consumer_success);
        outcome->failed = inflight_timeout != 0
                              ? EBUSY
                              : (result != 0 ? 0 : EIO);
        outcome->observed_write = result;
        /* R3a-2/B7：最后一次物理步骤/errno 进回执（可证伪诊断）。 */
        outcome->last_step = inflight_timeout != 0
                                 ? GLK43499_CARRIER_STEP_INFLIGHT
                                 : GLK43499_CARRIER_STEP_IO;
        outcome->last_errno =
            inflight_timeout != 0 ? EBUSY : last_stamp_errno;
    }
}

int32_t glk43499_carrier_multicast_disarm(struct glk43499_carrier *carrier,
                                          struct glk43499_pi_shared *shared) {
    struct glk43499_carrier_multicast *multicast =
        &carrier->value.multicast_sendmsg;
    atomic_store(&shared->consumer_go, 0);
    /* R3/B7：真实 D3 ghost disarm 已由 waiter 在路由后以请求参数执行
     * （actor.c waiter_multicast）；此处不再多发一次 stamp —— 审计指出该
     * 额外 stamp 会污染窗口外时序且不属于 legacy disarm 语义。 */
    /* Step B1（legacy route_multicast.cpp:149-155）：无界等 inflight 再返回。 */
    while (atomic_load(&shared->consumer_inflight) != 0) {
        __asm__ volatile("yield" ::: "memory");
    }
    if (0) {
        if (atomic_load(&shared->consumer_inflight) == 0) return 1;
        __asm__ volatile("yield" ::: "memory");
    }
    return atomic_load(&shared->consumer_inflight) == 0 ? 1 : 0;
}

int32_t glk43499_carrier_multicast_release(
    struct glk43499_carrier_multicast *multicast) {
    int32_t clean = 1;
    if (multicast->socket_fd >= 0) {
        if (close(multicast->socket_fd) != 0) clean = 0;
        multicast->socket_fd = -1;
    }
    free(multicast->frame);
    multicast->frame = NULL;
    multicast->frame_size = 0;
    multicast->operation_budget = 0;
    multicast->arm_hold_ns = 0;
    multicast->arm_sequence = 0;
    multicast->disarm_spin_limit = 0;
    multicast->prepared = 0;
    return clean;
}
