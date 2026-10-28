/**
 * @file placement.h
 * @brief 顺序敏感的 43499 Placement 事务契约。
 *
 * @note ABI v4 起无 reserved 槽；新增参数只允许在结构尾部 append-only 追加。
 * @note 阶段：ABI v4（43499 Native 全参数化 R2a）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PLACEMENT_H
#define GHOSTLOCK_GL43499_CORE_PLACEMENT_H

#include <stdint.h>

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按 legacy floor 语义派生 `objects_per_slab`。
 *
 * @param allocation_size 非零分配大小（字节）。
 * @param mm_struct_size 非零 `mm_struct` 尺寸（字节）。
 * @param out_objects 非空；成功写 floor 结果。
 * @return 1 成功；0 参数非法/结果不可表示（0 或超出 `UINT32_MAX`）。
 *
 * @note 等式：`objects = allocation_size / mm_struct_size`（C 整数除法，截断）。
 * @note A301SO 1024/32768 ⇒ 32；mondrian 5.10 960/32768 ⇒ 34（非整除，余 128）。
 * @note 本助手不分配资源、不读 profile；调用方仍需校验计数形状与容量上限。
 */
static inline int32_t glk43499_placement_objects_per_slab(
    uint64_t allocation_size, uint64_t mm_struct_size, uint32_t *out_objects) {
    if (out_objects == NULL || allocation_size == 0 ||
        mm_struct_size == 0 || mm_struct_size > allocation_size) {
        return 0;
    }
    const uint64_t objects = allocation_size / mm_struct_size;
    if (objects == 0 || objects > UINT32_MAX) return 0;
    *out_objects = (uint32_t) objects;
    return 1;
}

/**
 * @brief Kotlin 已解析并验证后的 Placement 物理参数。
 * @note Native 不读取 profile、不填默认值；所有计数和边界均须显式给出。
 * @note struct_size 必须是 sizeof(struct glk43499_placement_request)；Native 严格
 *       相等校验，不匹配 ⇒ INVALID_ARGUMENT/BAD_ABI 且不创建对象。
 * @note `mm_struct_size` 不要求整除 `allocation_size`（5.10 legacy floor 语义）：
 *       `objects_per_slab` 必须等于 `allocation_size / mm_struct_size`（整数除法）；
 *       A301SO 1024/32768 整除路径数值不变，mondrian 960/32768 ⇒ 34。
 */
struct glk43499_placement_request {
    struct glk43499_abi_header abi;
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t required_alignment;
    uint64_t scan_lower_bound;
    uint64_t scan_upper_bound;
    uint64_t scan_step;
    uint64_t scan_round_limit;
    uint64_t timeout_ns;
    uint32_t placement_cpu;
    uint32_t measurement_cpu;
    uint32_t objects_per_slab;
    uint32_t prepare_object_count;
    uint32_t spray_object_count;
    uint32_t pre_object_count;
    uint32_t post_object_count;
    /* R2a：原 reserved32 已删除（自然 4B 对齐垫不存在于字段列表）。 */
    /* P0a append-only：目标/时序参数（Native 不得回退默认值；缺失由 JNI 拒绝）。 */
    uint64_t slab_order;
    uint64_t mm_struct_size;
    uint64_t mm_partials;
    uint64_t prepare_slabs;
    uint64_t snitch_collisions;
    uint64_t poll_us;
    /* A 决策（R2b append-only）：原 STRUCT_/WIRE_ 容量与协议值全部进请求字段。 */
    uint64_t identity_start;
    uint64_t identity_end;
    uint64_t leak_tag_nibble;
    uint64_t leak_tag_shift;
    uint64_t staging_capacity;
    uint64_t max_context_entries;
    uint64_t max_objects_per_slab;
    uint64_t max_measurement_rounds;
};

/** @brief Placement 成功后复制给 Kotlin 的只读物理事实。 */
struct glk43499_placement_facts {
    uint64_t candidate_page_base;
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t required_alignment;
    uint64_t measured_object_address;
    uint64_t measurement_rounds;
    uint64_t retained_child_count;
    uint64_t retained_descriptor_count;
};

/** @brief preparePlacement 的完整回执；handle 返回值为 0 时仍必须读取 call。 */
struct glk43499_placement_result {
    struct glk43499_call_result call;
    struct glk43499_placement_facts facts;
    /* task-100 append-only 机制诊断（数值枚举；不改任何控制流）。 */
    uint32_t diag_step;
    uint32_t diag_syscall;
    uint32_t diag_errno;
    /* task-109 append-only：leak child 状态证据（pid 复用/已退出判别）。 */
    uint32_t diag_child_reaped;
    int32_t diag_child_status;
    uint32_t diag_child_proc_exists;
    int32_t diag_child_uid;
    int32_t diag_open_errno;
    int32_t diag_child_pid;
};

/** @brief task-100 placement 失败步骤（数值枚举；append-only）。 */
enum glk43499_placement_diag_step {
    GLK43499_PLACEMENT_DIAG_STEP_UNKNOWN = 0,
    GLK43499_PLACEMENT_DIAG_STEP_ALLOC = 1,
    GLK43499_PLACEMENT_DIAG_STEP_PIN_CPU = 2,
    GLK43499_PLACEMENT_DIAG_STEP_CLONE_CHILD = 3,
    GLK43499_PLACEMENT_DIAG_STEP_OPEN_MEMFD = 4,
    GLK43499_PLACEMENT_DIAG_STEP_STAGING = 5,
    GLK43499_PLACEMENT_DIAG_STEP_SNITCH = 6,
    GLK43499_PLACEMENT_DIAG_STEP_CLEANUP = 7,
    GLK43499_PLACEMENT_DIAG_STEP_ACQUIRE = 8,
    GLK43499_PLACEMENT_DIAG_STEP_VALIDATE = 9,
    GLK43499_PLACEMENT_DIAG_STEP_MEASUREMENT_PIN = 11,
    GLK43499_PLACEMENT_DIAG_STEP_LEAK_CLONE = 12,
    GLK43499_PLACEMENT_DIAG_STEP_LEAK_OPEN = 13,
    GLK43499_PLACEMENT_DIAG_STEP_STOP = 14,
    GLK43499_PLACEMENT_DIAG_STEP_DRAIN = 15
};

/** @brief task-100 placement 失败 syscall（数值枚举；append-only）。 */
enum glk43499_placement_diag_syscall {
    GLK43499_PLACEMENT_DIAG_SYSCALL_NONE = 0,
    GLK43499_PLACEMENT_DIAG_SYSCALL_CLONE = 1,
    GLK43499_PLACEMENT_DIAG_SYSCALL_OPEN = 2,
    GLK43499_PLACEMENT_DIAG_SYSCALL_SCHED_SETAFFINITY = 3,
    GLK43499_PLACEMENT_DIAG_SYSCALL_PRCTL = 4,
    GLK43499_PLACEMENT_DIAG_SYSCALL_MMAP = 5,
    GLK43499_PLACEMENT_DIAG_SYSCALL_MINCORE = 6,
    GLK43499_PLACEMENT_DIAG_SYSCALL_KILL = 7,
    GLK43499_PLACEMENT_DIAG_SYSCALL_WAITPID = 8,
    GLK43499_PLACEMENT_DIAG_SYSCALL_MEMFD_CREATE = 9,
    GLK43499_PLACEMENT_DIAG_SYSCALL_FTRUNCATE = 10,
    GLK43499_PLACEMENT_DIAG_SYSCALL_MISC = 11
};

/**
 * @brief 创建并完成一次不可拆的定位事务。
 *
 * @param request 调用期借用的完整参数；返回前不再访问该指针。
 * @param result 必填输出；调用方预先填写 result.call.abi。
 * @return Placement handle；失败返回 GLK43499_INVALID_HANDLE。
 * @note 成功对象拥有 placement children、memfd、staging storage 与定位资源。
 */
glk43499_handle glk43499_prepare_placement(
    const struct glk43499_placement_request *request,
    struct glk43499_placement_result *result);

/**
 * @brief 请求停止尚未进入 Landing 提交段的 Placement。
 * @param placement 有效 Placement handle。
 * @param result 必填输出。
 * @return glk43499_call_status。
 * @note cancel 只停止并排空活动工作，不销毁 handle。
 */
int32_t glk43499_cancel_placement(
    glk43499_handle placement,
    struct glk43499_close_result *result);

/**
 * @brief 终结 Placement 或报告其被保留到进程退出。
 * @param placement 仍由调用方唯一持有的 Placement token；wrapper 在调用前置 0。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时未接受、调用方恢复
 *         原 token；一旦接受，无论回执为完整释放或保留到进程退出都消费 token。
 */
int32_t glk43499_close_placement(
    glk43499_handle placement,
    struct glk43499_close_result *result);

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_placement_request) == 216,
                       "ABI v4 placement_request layout drift (expected 216)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_placement_facts) == 64,
                       "ABI v4 placement_facts layout drift (expected 64)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_placement_result) == 136,
                       "ABI v4+diag placement_result layout drift (expected 112; task-100 append 12B+pad4)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PLACEMENT_H */
