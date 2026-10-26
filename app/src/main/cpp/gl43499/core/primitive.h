/**
 * @file primitive.h
 * @brief 三种物理 Carrier 的无业务语义参数。
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
    uint64_t reserved[4];
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
    uint32_t reserved32;
    uint64_t reserved[6];
};

/** @brief Pselect Carrier 的纯物理参数。 */
struct glk43499_pselect_spec {
    struct glk43499_primitive_addresses addresses;
    const uint8_t *frame_bytes;
    uint64_t frame_size;
    uint64_t descriptor_capacity;
    uint64_t operation_budget;
    uint64_t timeout_ns;
    uint64_t reserved[6];
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
    uint64_t reserved[6];
};

/**
 * @brief prepareActor 调用期借用的 tagged union。
 * @note prepareActor 必须校验 kind、深拷贝所选分支和 frame_bytes，并在线程
 *       创建前绑定具体 Carrier；未选分支必须保持全零。
 */
struct glk43499_primitive_spec {
    struct glk43499_abi_header abi;
    glk43499_primitive_kind kind;
    uint32_t reserved32;
    union {
        struct glk43499_multicast_spec multicast_sendmsg;
        struct glk43499_pselect_spec pselect;
        struct glk43499_tcp_zerocopy_spec tcp_zerocopy_receive;
    } value;
    uint64_t reserved[8];
};

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PRIMITIVE_H */
