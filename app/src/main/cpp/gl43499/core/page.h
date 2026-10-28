/**
 * @file page.h
 * @brief Placement 到受控内核 Page 的 Landing 契约。
 *
 * @note ABI v4 起无 reserved 槽；字段只允许在结构尾部 append-only 追加。
 * @note 阶段：ABI v4（43499 Native 全参数化 R2a）。
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
};

/**
 * @brief Kotlin 决定的 Landing 几何与有界执行参数。
 * @note expected_page_base 必须等于 PlacementFacts.candidate_page_base。
 * @note struct_size 必须是 sizeof(struct glk43499_landing_request)。
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
    /* P0a append-only：触页填充字节（0..255；Native 不填默认值）。 */
    uint64_t fill_byte;
    /* R3a-2/B6 append-only：页接受判据（legacy2 memory.cpp:210-227 的参数化）。
     * required=1 时要求 accepted=(candidate_page_base + offset) 的第 bit 位为 1，
     * 否则该页把偶数写到 selinux_state.initialized 上（旧实现会 panic）；
     * required=0 表示本 route 无此判据。全部来自请求，Native 不内置目标常量。 */
    uint64_t page_accept_required;
    uint64_t page_accept_offset;
    uint64_t page_accept_bit;
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
    /* R3a-2/B6：页接受判据的求值结果（可证伪回执；未启用时 checked=0）。 */
    uint64_t page_accept_value;
    uint32_t page_accept_checked;
    uint32_t page_accept_ok;
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
 * @note CLEAN_SUCCESS 消费 placement，调用方必须立即将旧 token 置 0；提交前
 *       NOT_APPLIED 仍由调用方持有。提交后的 CLEAN_FAILURE、DIRTY 或
 *       INDETERMINATE 均设置 receipt.placement_consumed，旧 token 不得复用；
 *       Native 清洁释放或将无法安全释放的资源保留到进程退出。
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
 * @brief 关闭尚未转移给 Actor 的 Page。
 * @param page 仍由调用方唯一持有的 Page token。
 * @param result 必填输出。
 * @return glk43499_call_status；调用方保证不与其它 Page 调用并发。
 * @note wrapper 在调用前置 0。输出 ABI 无效或 token 为 0 时入口未接受，调用方
 *       恢复原 token；一旦接受，无论回执为完整释放或保留到进程退出都消费 token。
 */
int32_t glk43499_close_page(
    glk43499_handle page,
    struct glk43499_close_result *result);

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_page_bytes) == 32,
                       "ABI v4 page_bytes layout drift (expected 32)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_landing_request) == 104,
                       "ABI v4+R3 landing_request layout drift (expected 104)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_landing_receipt) == 88,
                       "ABI v4+R3 landing_receipt layout drift (expected 88)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_landing_result) == 120,
                       "ABI v4+R3 landing_result layout drift (expected 120)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PAGE_H */
