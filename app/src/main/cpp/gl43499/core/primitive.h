/**
 * @file primitive.h
 * @brief 三种物理 Carrier 的无业务语义参数。
 *
 * @note ABI v4 起无 reserved 槽；字段只允许在结构尾部 append-only 追加。
 *       未选中的 union 分支覆盖的尾字节必须为零（由 prepareActor 校验）。
 * @note 阶段：ABI v4（43499 Native 全参数化 R2a）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PRIMITIVE_H
#define GHOSTLOCK_GL43499_CORE_PRIMITIVE_H

#include <stdint.h>

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 物理 Carrier 种类；不是产品 route，也不表达 fallback。 */
typedef int32_t glk43499_primitive_kind;
enum {
    GLK43499_PRIMITIVE_INVALID = 0,
    GLK43499_PRIMITIVE_MULTICAST_SENDMSG = 1,
    GLK43499_PRIMITIVE_PSELECT = 2,
    GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE = 3
};

/** @brief 三种 Carrier 共享的最终地址；Native 不再从 offset 重算。 */
struct glk43499_primitive_addresses {
    uint64_t final_waiter_address;
    uint64_t final_task_address;
    uint64_t final_lock_address;
};

/** @brief Multicast Carrier 的纯物理参数。 */
struct glk43499_multicast_spec {
    struct glk43499_primitive_addresses addresses;
    const uint8_t *frame_bytes;
    uint64_t frame_size;
    uint64_t socket_buffer_size;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    uint32_t address_family;
    /* P0a append-only：arm 时序（ns 单位）与 arm 序列次数。 */
    uint64_t arm_hold_ns;
    uint64_t arm_sequence;
    /* A 决策（R2b）：帧容量上限与 ghost disarm 参数全部来自请求。 */
    uint64_t max_frame;
    int32_t ghost_pid;
    uint32_t futex_waiter_flag;
    uint32_t disarm_required;
};

/** @brief Pselect Carrier 的纯物理参数。 */
struct glk43499_pselect_spec {
    struct glk43499_primitive_addresses addresses;
    const uint8_t *frame_bytes;
    uint64_t frame_size;
    uint64_t descriptor_capacity;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    /* P0a append-only：尝试间延时（ns；R3a-2 起用于 attempt 间隔，不再当超时）。 */
    uint64_t per_attempt_delay_ns;
    /* A 决策（R2b）：nfds 容量上限来自请求（与 FD_SETSIZE 取小）。 */
    uint64_t max_nfds;
    /* R3a-2/P1 append-only：consumer 起跑延迟（ns；旧 enter_delay_us 语义）。 */
    uint64_t enter_delay_ns;
    /* R3a-2/P10：备份/安装用 fd 的起点步长（旧 nfds+64；不再用 FD_SETSIZE）。 */
    uint64_t fd_reserve_stride;
    /* R3a-2/P7：0=pselect(timeval 签名 pselect)，1=select(timeval)分支。 */
    uint64_t select_use_timeval;
};

/** @brief TCP zerocopy receive Carrier 的纯物理参数。 */
struct glk43499_tcp_zerocopy_spec {
    struct glk43499_primitive_addresses addresses;
    const uint8_t *frame_bytes;
    uint64_t frame_size;
    uint64_t mapping_size;
    uint64_t receive_size;
    uint64_t mapping_offset;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    /* P0a append-only：punch 自旋、post hold（ns）与 zerocopy 地址偏移。 */
    uint64_t punch_spin;
    uint64_t post_hold_ns;
    uint64_t zc_address_offset;
    /* A 决策（R2b）：fill_byte 唯一归属 landing_request（此处删除）；
     * 映射/帧容量上限改为请求字段。 */
    uint64_t max_mapping;
    uint64_t max_frame;
    /* R3a-2/T4 append-only：arm 序列（旧 >=16 才 arm；Native 不内置次数）。 */
    uint64_t arm_sequence;
    /* R3a-2/T2/T3 append-only：zc 请求体四个字段的最终偏移（旧三版统一
     * 0x18/0x20/0x28/0x30；白名单属 Kotlin generator，Native 只校验
     * 对齐/界内/互不重叠，T5）。 */
    uint64_t zc_length_offset;
    uint64_t zc_task_offset;
    uint64_t zc_lock_offset;
    /* R3a-2：punch 相位等待的独立自旋上限（不复用 disarm 上限）。 */
    uint64_t punch_phase_spin;
    /* R3b append-only：发送小包与 zerocopy 映射页的填充字节各有唯一来源
     * （legacy2 `route_tcp_session.cpp:61-64`：send=0x33 / mapping=0x55）。
     * 不得复用 landing_request.fill_byte（A 决策：每个物理使用者一个请求字段）。 */
    uint64_t send_fill_byte;
    uint64_t mapping_fill_byte;
};

/**
 * @brief prepareActor 调用期借用的 tagged union。
 * @note prepareActor 必须校验 kind、深拷贝所选分支和 frame_bytes，并在线程
 *       创建前绑定具体 Carrier；未选分支尾字节必须保持全零。
 * @note struct_size 必须是 sizeof(struct glk43499_primitive_spec)。
 */
struct glk43499_primitive_spec {
    struct glk43499_abi_header abi;
    glk43499_primitive_kind kind;
    union {
        struct glk43499_multicast_spec multicast_sendmsg;
        struct glk43499_pselect_spec pselect;
        struct glk43499_tcp_zerocopy_spec tcp_zerocopy_receive;
    } value;
};

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_primitive_addresses) == 24,
                       "ABI v4 primitive_addresses layout drift (expected 24)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_multicast_spec) == 112,
                       "ABI v4 multicast_spec layout drift (expected 112)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_pselect_spec) == 104,
                       "ABI v4+R3 pselect_spec layout drift (expected 104)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_tcp_zerocopy_spec) == 176,
                       "ABI v4+R3 tcp_zerocopy_spec layout drift (expected 176)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_primitive_spec) == 192,
                       "ABI v4+R3 primitive_spec layout drift (expected 192)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PRIMITIVE_H */
