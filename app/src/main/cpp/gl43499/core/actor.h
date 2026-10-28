/**
 * @file actor.h
 * @brief 消费 Page、绑定一个 Carrier 的一次性 PI Actor 契约。
 *
 * @note ABI v4 起无 reserved 槽；字段只允许在结构尾部 append-only 追加。
 * @note 阶段：ABI v4（43499 Native 全参数化 R2a）。
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
    uint64_t prepare_timeout_ns;
    uint64_t parked_timeout_ns;
    uint64_t execution_timeout_ns;
    uint64_t shutdown_timeout_ns;
    /* P0a append-only：PI 竞态与等待参数（Native 不得回退默认值）。 */
    uint64_t consumer_max;
    uint64_t consumer_burst;
    uint64_t lock_pi_spin_ns;
    uint64_t poll_us;
    uint64_t setup_settle_us;
    int64_t requeue_expected;
    uint64_t disarm_spin_limit;
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
    /* R3a-2 append-only：Carrier 中性操作计数与失败定位（可证伪诊断）。
     * last_step 语义见 carrier_internal.h 的 glk43499_carrier_step；last_errno
     * 为最后一次物理 syscall 的 errno 语义错误码（0 表示未记录）。 */
    uint64_t carrier_operations;
    int32_t carrier_error;
    int32_t carrier_last_step;
    int32_t carrier_last_errno;
    int32_t carrier_punch_failed;
    /* R3a-2/B7 append-only：multicast ghost disarm 的 FUTEX_LOCK_PI ret/errno
     * （disarm_required=0 时保持 0；旧实现不可核验）。 */
    int32_t ghost_disarm_result;
    int32_t ghost_disarm_errno;
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
    /* task-102 append-only 阶段诊断（数值枚举；PI 窗口内不 IO/不日志，只回执字段）。 */
    uint32_t diag_stage;
    int32_t diag_requeue_ret;
    int32_t diag_requeue_errno;
    /* task-105 append-only v3：首次失败阶段（冻结；cleanup 不覆写）+ 调用计数。 */
    uint32_t diag_fail_stage;
    uint32_t diag_publish_count;
    uint32_t diag_requeue_calls;
    /* task-107 append-only v4：版本 marker + requeue 前 waiter/owner 就绪快照。 */
    uint32_t diag_version;
    uint32_t diag_waiter_waiting;
    uint32_t diag_owner_started;
    uint32_t diag_requeue_expected;
    /* task-108 append-only v5：waiter futex 证据 + owner/target 时序快照。 */
    int32_t diag_waiter_wait_ret;
    int32_t diag_waiter_wait_errno;
    uint32_t diag_waiter_in_futex;
    uint32_t diag_waiter_dispatched;
    uint32_t diag_target_value_at_requeue;
    uint64_t diag_ts_settle_done_ns;
    uint64_t diag_ts_waiter_enter_ns;
    uint64_t diag_ts_waiter_exit_ns;
    uint64_t diag_ts_requeue_ns;
    /* task-110 append-only v6：futex 实参/时序。 */
    uint64_t diag_parked_timeout_ns;
    uint64_t diag_setup_settle_us;
    uint64_t diag_poll_us;
    uint64_t diag_prepare_timeout_ns;
    int64_t diag_ts_sec;
    int64_t diag_ts_nsec;
    uint32_t diag_futex_op;
    uint32_t diag_futex_val;
    uint32_t diag_futex_uaddr2;
    uint32_t diag_futex_val3;
    int32_t diag_owner_tid;
};

/** @brief task-102 actor execute 失败阶段（数值枚举；append-only）。 */
enum glk43499_actor_diag_stage {
    GLK43499_ACTOR_DIAG_STAGE_UNKNOWN = 0,
    GLK43499_ACTOR_DIAG_STAGE_SETTLE = 1,
    GLK43499_ACTOR_DIAG_STAGE_PUBLISH = 2,
    GLK43499_ACTOR_DIAG_STAGE_REQUEUE = 3,
    GLK43499_ACTOR_DIAG_STAGE_WAIT_WINDOW = 4,
    GLK43499_ACTOR_DIAG_STAGE_WINDOW_DONE = 5,
    GLK43499_ACTOR_DIAG_STAGE_DRAIN = 6,
    GLK43499_ACTOR_DIAG_STAGE_DISARM = 7
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
 * @brief task-111 机制自测：最小三线程 FUTEX_WAIT_REQUEUE_PI/CMP_REQUEUE_PI 复现（debug-only）。
 * @param variant 0=val3=0；1=val3=owner_tid；2=waiter CLOCK_REALTIME 绝对 deadline。
 * @param out 长度 >=10 的 int32 输出：variant/owner_ret/owner_errno/owner_tid/waiter_ret/
 *   waiter_errno/waiter_elapsed_us/requeue_ret/requeue_errno/waiter_woke。
 * @return 0=完成（结果见 out）；-1=线程创建失败。
 * @note 只做 futex 机制实验，不碰 W1/内核写/flags/mode；结果经 debug JNI 输出 logcat。
 */
int32_t glk43499_actor_futex_selftest(int32_t variant, int32_t *out);

/**
 * @brief task-113 debug-only：读取 actor carrier 崩溃前快照（机制字段）。
 * @param actor Actor handle。
 * @param out 长度>=8 的 int64 输出：state/waiter_waiting/window_opened/window_done/
 *   carrier_started/frames_sent/step/last_errno；needs >=10 for route enter/exit.
 * @return 0=成功；-1=handle 无效。
 * @note 只读 atomic，不做 IO/日志；仅 debug 诊断入口使用。
 */
int32_t glk43499_actor_debug_snapshot(glk43499_handle actor, int64_t *out);

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

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_actor_setup) == 128,
                       "ABI v4 actor_setup layout drift (expected 128)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_actor_prepare_receipt) == 40,
                       "ABI v4 actor_prepare_receipt layout drift (expected 40)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_actor_execute_receipt) == 88,
                       "ABI v4+R3 actor_execute_receipt layout drift (expected 88)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_actor_prepare_result) == 72,
                       "ABI v4 actor_prepare_result layout drift (expected 72)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_actor_execute_result) == 288,
                       "ABI v4+R3 actor_execute_result layout drift (expected 144; task-105 diag append)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_ACTOR_H */
