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

/** @brief Multicast 操作预算上限；超过即拒绝（单发路线不需要更大预算）。 */
#define GLK43499_MULTICAST_MAX_BUDGET UINT64_C(4096)

/** @brief Multicast 超时上限（1 小时）；只做边界校验。 */
#define GLK43499_MULTICAST_MAX_TIMEOUT_NS UINT64_C(3600000000000)

/** @brief 声明的 socket 缓冲上限；只做边界校验，不参与物理判定。 */
#define GLK43499_MULTICAST_MAX_SOCKET_BUFFER (UINT64_C(1) << 22)

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
    if (spec->frame_size == 0 ||
        spec->frame_size > GLK43499_PHYS_MULTICAST_MAX_FRAME) {
        return -EINVAL;
    }
    if (spec->address_family != 0 && spec->address_family != AF_INET) {
        return -EINVAL;
    }
    if (spec->operation_budget == 0 ||
        spec->operation_budget > GLK43499_MULTICAST_MAX_BUDGET) {
        return -EINVAL;
    }
    if (spec->timeout_ns == 0 ||
        spec->timeout_ns > GLK43499_MULTICAST_MAX_TIMEOUT_NS) {
        return -EINVAL;
    }
    if (spec->socket_buffer_size > GLK43499_MULTICAST_MAX_SOCKET_BUFFER) {
        return -EINVAL;
    }
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
    atomic_store(&shared->consumer_calls, 0);
    atomic_store(&shared->consumer_success, 0);
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&shared->route_delay_usec, 0);
    const uint64_t deadline =
        glk43499_internal_deadline(multicast->timeout_ns);

    uint64_t operations = 0;
    for (uint64_t attempt = 1; attempt <= multicast->operation_budget;
         ++attempt) {
        if (glk43499_internal_expired(deadline)) break;
        ++operations;
        errno = 0;
        const int32_t stamp_result =
            setsockopt(multicast->socket_fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                       multicast->frame, (socklen_t) multicast->frame_size);
        const int32_t stamp_errno = errno;
        /* copy 先于 family 检查完成，因此 EADDRNOTAVAIL 仍表示 waiter 已上栈。 */
        if (stamp_result != 0 && stamp_errno != EADDRNOTAVAIL) continue;
        atomic_store(&shared->consumer_go, (int32_t) attempt);
        for (uint64_t spin = 0;
             spin < GLK43499_PHYS_MULTICAST_ARM_HOLD &&
             atomic_load(&shared->consumer_calls) == 0;
             ++spin) {
            __asm__ volatile("yield" ::: "memory");
        }
        atomic_store(&shared->consumer_go, 0);
        for (uint64_t spin = 0;
             spin < GLK43499_PHYS_DISARM_SPIN_LIMIT &&
             atomic_load(&shared->consumer_inflight) != 0;
             ++spin) {
            __asm__ volatile("yield" ::: "memory");
        }
        if (atomic_load(&shared->consumer_success) > 0) {
            result = 1;
            break;
        }
    }
    if (outcome != NULL) {
        outcome->operations = operations;
        outcome->calls = (uint64_t) atomic_load(&shared->consumer_calls);
        outcome->successes = (uint64_t) atomic_load(&shared->consumer_success);
        outcome->failed = result != 0 ? 0 : EIO;
        outcome->observed_write = result;
    }
}

int32_t glk43499_carrier_multicast_disarm(struct glk43499_carrier *carrier,
                                          struct glk43499_pi_shared *shared) {
    (void) carrier;
    atomic_store(&shared->consumer_go, 0);
    for (uint64_t spin = 0; spin < GLK43499_PHYS_DISARM_SPIN_LIMIT; ++spin) {
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
    multicast->prepared = 0;
    return clean;
}
