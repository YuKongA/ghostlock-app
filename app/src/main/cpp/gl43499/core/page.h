/**
 * @file page.h
 * @brief Placement 到受控内核 Page 的 Landing 契约。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PAGE_H
#define GHOSTLOCK_GL43499_CORE_PAGE_H

#include <stddef.h>
#include <stdint.h>

#include "placement.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 调用期借用的最终页面字节；Native 必须在返回前完成复制。 */
struct glk43499_page_bytes {
    struct glk43499_abi_header abi;
    const uint8_t *data;
    uint64_t size;
    uint64_t content_generation;
    uint64_t reserved[4];
};

/**
 * @brief Kotlin 决定的 Landing 几何与有界执行参数。
 * @note expected_page_base 必须等于 PlacementFacts.candidate_page_base。
 */
struct glk43499_landing_request {
    struct glk43499_abi_header abi;
    uint64_t expected_page_base;
    uint64_t page_size;
    uint64_t allocation_size;
    uint64_t required_alignment;
    uint64_t reclaim_payload_size;
    uint64_t socket_send_buffer_size;
    uint64_t reclaim_send_limit;
    uint64_t timeout_ns;
    uint64_t reserved[8];
};

/** @brief Landing 的提交点与实际物理操作计数。 */
struct glk43499_landing_receipt {
    uint64_t actual_page_base;
    uint64_t content_generation;
    uint64_t closed_prepare_count;
    uint64_t closed_spray_count;
    uint64_t closed_pre_count;
    uint64_t closed_post_count;
    uint64_t reclaim_send_count;
    uint32_t commit_reached;
    uint32_t placement_consumed;
    uint32_t reclaim_retained;
    uint32_t reserved32;
    uint64_t reserved[8];
};

/** @brief landPage 输出；返回 Page handle 为 0 时仍须读取 call 和 receipt。 */
struct glk43499_landing_result {
    struct glk43499_call_result call;
    struct glk43499_landing_receipt receipt;
};

/**
 * @brief 将 Kotlin 构造的最终字节落入 Placement 指定的候选位置。
 *
 * @param placement 有效且未被并发使用的 Placement handle。
 * @param bytes 调用期借用；data 非空且 size 必须等于 reclaim_payload_size。
 * @param request 调用期借用的最终几何。
 * @param result 必填输出。
 * @return 成功时返回新的 Page handle；其余返回 0。
 * @note CLEAN_SUCCESS 消费 placement 并使其 stale；提交前 NOT_APPLIED 保留
 *       placement；提交后不明状态将 placement 转为 QUARANTINED。
 * @note allocation_size 是候选分配大小，page_size 是地址布局边界，
 *       reclaim_payload_size 是本次内核复制字节数；三者都由 Kotlin 明列。
 *       Native 只接受实现已证明的大小关系，且不从其中任一字段推导默认值。
 */
glk43499_handle glk43499_land_page(
    glk43499_handle placement,
    const struct glk43499_page_bytes *bytes,
    const struct glk43499_landing_request *request,
    struct glk43499_landing_result *result);

/**
 * @brief 关闭未被 Actor 借用的 Page。
 * @param page 有效 Page handle。
 * @param result 必填输出。
 * @return GLK43499_CALL_BUSY 表示存在 persistent borrow 且本调用不等待。
 * @note 引用状态不明时保留完整资源闭包到进程退出。
 */
int32_t glk43499_close_page(
    glk43499_handle page,
    struct glk43499_close_result *result);

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PAGE_H */
