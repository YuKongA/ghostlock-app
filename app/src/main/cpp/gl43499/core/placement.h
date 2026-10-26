/**
 * @file placement.h
 * @brief 顺序敏感的 43499 Placement 事务契约。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PLACEMENT_H
#define GHOSTLOCK_GL43499_CORE_PLACEMENT_H

#include <stdint.h>

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Kotlin 已解析并验证后的 Placement 物理参数。
 * @note Native 不读取 profile、不填默认值；所有计数和边界均须显式给出。
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
    uint32_t reserved32;
    uint64_t reserved[8];
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
    uint64_t reserved[8];
};

/** @brief preparePlacement 的完整回执；handle 返回值为 0 时仍必须读取 call。 */
struct glk43499_placement_result {
    struct glk43499_call_result call;
    struct glk43499_placement_facts facts;
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
 * @brief 终结 Placement 或报告其被保留隔离。
 * @param placement 有效 Placement handle；成功后变为 stale。
 * @param result 必填输出。
 * @return glk43499_call_status；LANDING/未知引用转 QUARANTINED。
 */
int32_t glk43499_close_placement(
    glk43499_handle placement,
    struct glk43499_close_result *result);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PLACEMENT_H */
