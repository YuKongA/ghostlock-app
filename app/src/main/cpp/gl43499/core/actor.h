/**
 * @file actor.h
 * @brief 借用 Page、绑定一个 Carrier 的一次性 PI Actor 契约。
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

/** @brief prepareActor 返回的 Page 借用与 Carrier 绑定事实。 */
struct glk43499_actor_prepare_receipt {
    uint64_t page_generation;
    glk43499_primitive_kind primitive_kind;
    uint32_t thread_count;
    uint32_t page_borrow_acquired;
    uint32_t owner_ready;
    uint32_t waiter_ready;
    uint32_t consumer_ready;
    uint32_t carrier_prepared;
    uint32_t reserved32;
    uint64_t reserved[8];
};

/** @brief 一次 Actor execute 的纯物理观测。 */
struct glk43499_actor_execute_receipt {
    uint64_t page_generation;
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
 * @brief 创建 Actor、借用 Page，并在线程创建前绑定具体 Carrier。
 *
 * @param page 有效且未被其它独占 Actor 借用的 Page handle。
 * @param setup 调用期借用；thread_count 必须为 3。
 * @param primitive 调用期借用；选中分支及 frame bytes 被深拷贝。
 * @param result 必填输出。
 * @return Actor handle；失败返回 0，且部分创建的线程必须已停止并 join。
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
 * @note 本入口没有 route/tag/spec 参数；PI 窗口内不读取 registry、不分配、
 *       不打印，也不进行动态机制分派。
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
 * @brief 按固定顺序终结 Actor 并归还 Page 借用。
 * @param actor 有效 Actor handle；成功后变为 stale。
 * @param result 必填输出。
 * @return glk43499_call_status。
 * @note 顺序为停止新触发、等待 consumer inflight 为零、disarm、解除 PI 关联、
 *       按经验证顺序 join waiter/owner/consumer、关闭 Carrier、归还 Page。未知引用使 Actor、
 *       Carrier 与 Page 整体 QUARANTINED。
 */
int32_t glk43499_close_actor(
    glk43499_handle actor,
    struct glk43499_close_result *result);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_ACTOR_H */
