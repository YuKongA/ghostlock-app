/**
 * @file actor.h
 * @brief 消费 Page、绑定一个 Carrier 的一次性 PI Actor 契约。
 */
#ifndef GHOSTLOCK_GL43499_CORE_ACTOR_H
#define GHOSTLOCK_GL43499_CORE_ACTOR_H

#include <stdint.h>

#include "page.h"
#include "primitive.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 三线程 Actor 的 CPU、优先级与有界等待参数。 */
struct glk43499_actor_setup {
    struct glk43499_abi_header abi;
    uint32_t owner_cpu;
    uint32_t waiter_cpu;
    uint32_t consumer_cpu;
    int32_t owner_priority;
    int32_t waiter_priority;
    int32_t consumer_priority;
    uint32_t thread_count;
    uint32_t reserved32;
    uint64_t prepare_timeout_ns;
    uint64_t parked_timeout_ns;
    uint64_t execution_timeout_ns;
    uint64_t shutdown_timeout_ns;
    uint64_t reserved[8];
};

/** @brief prepareActor 返回的 Page 所有权转移与 Carrier 绑定事实。 */
struct glk43499_actor_prepare_receipt {
    uint64_t page_content_generation;
    glk43499_primitive_kind primitive_kind;
    uint32_t thread_count;
    uint32_t page_consumed;
    uint32_t owner_ready;
    uint32_t waiter_ready;
    uint32_t consumer_ready;
    uint32_t carrier_prepared;
    uint32_t reserved32;
    uint64_t reserved[8];
};

/** @brief 一次 Actor execute 的纯物理观测。 */
struct glk43499_actor_execute_receipt {
    uint64_t page_content_generation;
    uint64_t consumer_call_count;
    uint64_t consumer_success_count;
    uint64_t consumer_inflight_at_return;
    uint32_t owner_ready;
    uint32_t waiter_parked;
    uint32_t carrier_armed;
    uint32_t window_opened;
    uint32_t carrier_disarmed;
    uint32_t retained_for_process_exit;
    uint32_t reserved32;
    uint64_t reserved[8];
};

/** @brief prepareActor 输出。 */
struct glk43499_actor_prepare_result {
    struct glk43499_call_result call;
    struct glk43499_actor_prepare_receipt receipt;
};

/** @brief executeActor 输出。 */
struct glk43499_actor_execute_result {
    struct glk43499_call_result call;
    struct glk43499_actor_execute_receipt receipt;
};

/**
 * @brief 创建 Actor、消费 Page，并在线程创建前绑定具体 Carrier。
 *
 * @param page 由调用方唯一持有且尚未消费的 Page token。
 * @param setup 调用期借用；thread_count 必须为 3。
 * @param primitive 调用期借用；选中分支及 frame bytes 被深拷贝。
 * @param result 必填输出。
 * @return Actor token；成功时 Actor 接管 Page，调用方必须将 Page token 置 0；
 *         clean 失败返回 0 且 Page 仍归调用方；dirty/indeterminate 失败时
 *         receipt.page_consumed 为 1，调用方也必须置 0，Native 保留资源到进程退出。
 */
glk43499_handle glk43499_prepare_actor(
    glk43499_handle page,
    const struct glk43499_actor_setup *setup,
    const struct glk43499_primitive_spec *primitive,
    struct glk43499_actor_prepare_result *result);

/**
 * @brief 执行 prepare 时已绑定的物理 Carrier。
 * @param actor PREPARED 状态的 Actor handle。
 * @param result 必填输出。
 * @return glk43499_call_status。
 * @note 本入口没有 route/tag/spec 参数；PI 窗口内不分配、不打印，也不进行动态机制分派。
 */
int32_t glk43499_execute_actor(
    glk43499_handle actor,
    struct glk43499_actor_execute_result *result);

/**
 * @brief 请求停止新触发并排空正在执行的 Carrier 操作。
 * @param actor 有效 Actor handle。
 * @param result 必填输出。
 * @return glk43499_call_status；不销毁 Actor。
 */
int32_t glk43499_cancel_actor(
    glk43499_handle actor,
    struct glk43499_close_result *result);

/**
 * @brief 按固定顺序终结 Actor 及其拥有的 Page。
 * @param actor 由调用方唯一持有的 Actor token；wrapper 在调用前置 0。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时未接受、调用方恢复
 *         原 token；一旦接受，无论释放或保留结果都消费 token。
 * @note 顺序为停止新触发、等待 consumer inflight 为零、disarm、解除 PI 关联、
 *       按经验证顺序 join waiter/owner/consumer、关闭 Carrier、释放其拥有的 Page。
 *       未知引用使 Actor、Carrier 与 Page 整体保留到进程退出。
 */
int32_t glk43499_close_actor(
    glk43499_handle actor,
    struct glk43499_close_result *result);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_ACTOR_H */
