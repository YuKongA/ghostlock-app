/**
 * @file result.h
 * @brief 43499 调用状态、物理效果与资源状态的正交回执。
 */
#ifndef GHOSTLOCK_GL43499_CORE_RESULT_H
#define GHOSTLOCK_GL43499_CORE_RESULT_H

#include <stdint.h>

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief API 调用本身的结果；存储字段固定为 int32_t。 */
typedef int32_t glk43499_call_status;
enum {
    GLK43499_CALL_OK = 0,
    GLK43499_CALL_INVALID_ARGUMENT = 1,
    GLK43499_CALL_BAD_ABI = 2,
    GLK43499_CALL_STALE_HANDLE = 3,
    GLK43499_CALL_WRONG_HANDLE_KIND = 4,
    GLK43499_CALL_BUSY = 5,
    GLK43499_CALL_TIMEOUT = 6,
    GLK43499_CALL_CANCELLED = 7,
    GLK43499_CALL_SYSTEM_ERROR = 8,
    GLK43499_CALL_UNSUPPORTED = 9,
    GLK43499_CALL_INTERNAL_ERROR = 10
};

/** @brief 一次物理事务是否改变了受管资源；不表达业务目标是否达成。 */
typedef int32_t glk43499_effect;
enum {
    GLK43499_EFFECT_NONE = 0,
    GLK43499_EFFECT_NOT_APPLIED = 1,
    GLK43499_EFFECT_CLEAN_SUCCESS = 2,
    GLK43499_EFFECT_CLEAN_FAILURE = 3,
    GLK43499_EFFECT_INDETERMINATE = 4,
    GLK43499_EFFECT_DIRTY = 5
};

/** @brief 调用返回时的资源生命周期状态。 */
typedef int32_t glk43499_resource_state;
enum {
    GLK43499_RESOURCE_INVALID = 0,
    GLK43499_RESOURCE_OPEN = 1,
    GLK43499_RESOURCE_PREPARED = 2,
    GLK43499_RESOURCE_LANDING = 3,
    GLK43499_RESOURCE_LANDED = 4,
    GLK43499_RESOURCE_BORROWED = 5,
    GLK43499_RESOURCE_RUNNING = 6,
    GLK43499_RESOURCE_CONSUMED = 7,
    GLK43499_RESOURCE_CLOSING = 8,
    GLK43499_RESOURCE_CLOSED = 9,
    GLK43499_RESOURCE_QUARANTINED = 10
};

/**
 * @brief 所有入口共有的回执前缀。
 * @note system_error 仅在 call_status 为 GLK43499_CALL_SYSTEM_ERROR 时有意义。
 */
struct glk43499_call_result {
    struct glk43499_abi_header abi;
    glk43499_call_status call_status;
    glk43499_effect effect;
    glk43499_resource_state resource_state;
    int32_t system_error;
    uint64_t generation;
    uint64_t retained_resource_count;
    uint64_t reserved[4];
};

/** @brief close/cancel 的资源终结回执。 */
struct glk43499_close_result {
    struct glk43499_call_result call;
    uint32_t stop_requested;
    uint32_t workers_joined;
    uint32_t borrow_released;
    uint32_t retained_for_process_exit;
    uint64_t reserved[4];
};

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_RESULT_H */
