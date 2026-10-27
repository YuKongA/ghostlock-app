/**
 * @file core_internal.h
 * @brief 43499 内部实现共用的 ABI 校验、回执写入、时钟与 CPU 绑定助手。
 *
 * @note 本头只放无状态 `static inline` 助手与具名常量；不定义可写全局、
 *       不保存资源、不读取 profile。所有线程/PI 状态保存在调用方对象内。
 * @note 阶段：G4L-1..G4L-4T 纯代码冲刺的内部实现层；公共 C11 契约仍是
 *       `api.h` 聚合的那七个头，本文件不对外发布。
 */
#ifndef GHOSTLOCK_GL43499_CORE_CORE_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_CORE_INTERNAL_H

#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "abi.h"
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 校验由调用方初始化的 ABI 头。
 * @param header 调用方提供的头；可为 NULL。
 * @param minimum_size 本入口要求的最小结构宽度。
 * @return 1 表示版本正确、宽度足够且 reserved 全零；0 表示拒绝。
 * @note 前提：只在读其它字段之前调用。失败语义：仅返回 0，不写任何内存。
 * @note 阶段：每个入口的第一道校验（输出 ABI 优先、输入 ABI 其次）。
 */
static inline int32_t glk43499_internal_valid_header(
    const struct glk43499_abi_header *header, uint32_t minimum_size) {
    return header != NULL && header->struct_size >= minimum_size &&
           header->abi_version == GLK43499_ABI_VERSION &&
           header->reserved[0] == 0 && header->reserved[1] == 0;
}

/**
 * @brief 校验固定宽度保留字段是否全零。
 * @param values 保留字段数组；可为 NULL 仅当 count 为 0。
 * @param count 字段个数。
 * @return 1 全零；0 存在非零。
 * @note 阶段：输入 ABI 校验的一部分；不做范围推导。
 */
static inline int32_t glk43499_internal_zero_u64(const uint64_t *values,
                                                 size_t count) {
    if (values == NULL) return count == 0 ? 1 : 0;
    for (size_t index = 0; index < count; ++index) {
        if (values[index] != 0) return 0;
    }
    return 1;
}

/**
 * @brief 校验输出结构的 ABI 前缀（`call.abi`）。
 * @param call 调用方提供的输出回执；可为 NULL。
 * @param minimum_size 本入口会写出的最小结构宽度（通常为整个输出结构）。
 * @return 1 可写；0 表示输出 ABI 无效，入口必须未接受 token。
 * @note 阶段：所有入口最前面的动作；失败时不写输出边界外内存。
 */
static inline int32_t glk43499_internal_output_abi(
    const struct glk43499_call_result *call, uint32_t minimum_size) {
    return call != NULL &&
           glk43499_internal_valid_header(&call->abi, minimum_size);
}

/**
 * @brief 清零输出结构的非 ABI 部分并写入统一回执。
 * @param call 已通过输出 ABI 校验的回执。
 * @param status 调用状态。
 * @param effect 物理效果。
 * @param state 资源状态。
 * @param system_error 仅 SYSTEM_ERROR 有意义；其余写 0。
 * @param retained 保留到进程退出的资源数。
 * @note 前提：`call->abi` 已被调用方初始化且未在本函数内改写。
 * @note 阶段：每个入口的出口；不改写调用方填写的 abi 字段。
 */
static inline void glk43499_internal_set_call(
    struct glk43499_call_result *call, glk43499_call_status status,
    glk43499_effect effect, glk43499_resource_state state,
    int32_t system_error, uint64_t retained) {
    call->call_status = status;
    call->effect = effect;
    call->resource_state = state;
    call->system_error = system_error;
    call->reserved_resource_serial = 0;
    call->retained_resource_count = retained;
    for (size_t index = 0;
         index < sizeof(call->reserved) / sizeof(call->reserved[0]); ++index) {
        call->reserved[index] = 0;
    }
}

/**
 * @brief 清零 close/cancel 回执的终结计数部分并写统一回执。
 * @param result 输出回执；调用前已通过输出 ABI 校验。
 * @param status 调用状态。
 * @param effect 物理效果。
 * @param state 资源状态。
 * @param system_error 仅 SYSTEM_ERROR 有意义。
 * @param stop_requested 是否已请求停止新触发。
 * @param workers_joined 是否已按经验证顺序 join 全部 worker。
 * @param owned_chain_released 是否已释放完整所有权链。
 * @param retained 是否保留闭包到进程退出。
 * @note 阶段：close/cancel 的出口；把 reserved 全部写零。
 */
static inline void glk43499_internal_set_close(
    struct glk43499_close_result *result, glk43499_call_status status,
    glk43499_effect effect, glk43499_resource_state state, int32_t system_error,
    uint32_t stop_requested, uint32_t workers_joined,
    uint32_t owned_chain_released, uint32_t retained) {
    glk43499_internal_set_call(&result->call, status, effect, state,
                               system_error, retained ? 1U : 0U);
    result->stop_requested = stop_requested;
    result->workers_joined = workers_joined;
    result->owned_chain_released = owned_chain_released;
    result->retained_for_process_exit = retained;
    for (size_t index = 0;
         index < sizeof(result->reserved) / sizeof(result->reserved[0]);
         ++index) {
        result->reserved[index] = 0;
    }
}

/**
 * @brief 把不透明 token 还原为 Native 对象指针。
 * @param handle 非零 token；调用方必须已确认非零。
 * @return 对象指针；0 返回 NULL。
 * @note 不读取对象内容来推断 kind 或存活性：读已释放对象本身就是 UAF，
 *       线性 owner 契约把误用责任放在调用方。
 */
static inline void *glk43499_internal_handle_ptr(glk43499_handle handle) {
    return (void *) (uintptr_t) handle;
}

/**
 * @brief 把 Native 对象指针转换为不透明 token。
 * @param pointer 堆对象地址；可为 NULL。
 * @return token；NULL 返回 0。
 */
static inline glk43499_handle glk43499_internal_ptr_handle(const void *pointer) {
    return (glk43499_handle) (uintptr_t) pointer;
}

/**
 * @brief 读取单调时钟纳秒。
 * @return 单调时钟纳秒；失败返回 0（超时判断退化为已到期）。
 * @note 阶段：prepare/execute/close 的 deadline 计算。
 */
static inline uint64_t glk43499_internal_now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t) now.tv_sec * UINT64_C(1000000000) +
           (uint64_t) now.tv_nsec;
}

/**
 * @brief 计算相对超时的绝对 deadline。
 * @param timeout_ns 相对超时纳秒；调用方已校验非零。
 * @return 绝对单调时钟纳秒。
 */
static inline uint64_t glk43499_internal_deadline(uint64_t timeout_ns) {
    return glk43499_internal_now_ns() + timeout_ns;
}

/**
 * @brief 判断 deadline 是否已到期。
 * @param deadline_ns 绝对单调时钟纳秒；0 表示立即到期。
 * @return 1 已到期；0 未到期。
 */
static inline int32_t glk43499_internal_expired(uint64_t deadline_ns) {
    if (deadline_ns == 0) return 1;
    return glk43499_internal_now_ns() >= deadline_ns ? 1 : 0;
}

/**
 * @brief 把纳秒超时转换为相对 timespec。
 * @param timeout_ns 相对超时纳秒。
 * @param out 输出 timespec；不能为 NULL。
 * @note 前提：调用方已校验 out 非空。
 */
static inline void glk43499_internal_timespec(uint64_t timeout_ns,
                                              struct timespec *out) {
    out->tv_sec = (time_t) (timeout_ns / UINT64_C(1000000000));
    out->tv_nsec = (long) (timeout_ns % UINT64_C(1000000000));
}

/** @brief 亲和性掩码位宽上限（与内核 `CPU_SETSIZE` 的 1024 位一致）。 */
#define GLK43499_INTERNAL_CPU_MASK_BITS 1024U

/**
 * @brief 校验 CPU 编号是否落在本进程可用集合内。
 * @param cpu Kotlin 给出的 CPU 编号。
 * @return 1 有效；0 拒绝。
 * @note 依据：必须小于本机配置的 CPU 数，且不超过亲和性掩码位宽。
 */
static inline int32_t glk43499_internal_valid_cpu(uint32_t cpu) {
    if (cpu >= GLK43499_INTERNAL_CPU_MASK_BITS) return 0;
    const long configured = sysconf(_SC_NPROCESSORS_CONF);
    if (configured <= 0) return cpu == 0 ? 1 : 0;
    return cpu < (uint32_t) configured ? 1 : 0;
}

/**
 * @brief 把当前线程绑定到指定 CPU。
 * @param cpu 已校验的 CPU 编号。
 * @return 0 成功；-1 失败（errno 保留）。
 * @note 用裸 `SYS_sched_setaffinity` 而不是 `cpu_set_t`：本头要在未打开
 *       `_GNU_SOURCE` 的 TU 内也可用，且不改变全局构建选项。
 * @note 阶段：worker 线程启动、placement 采集、landing 提交前。
 * @note 失败语义：调用方按对应批次的清理阶梯回滚，不忽略。
 */
static inline int32_t glk43499_internal_pin_cpu(uint32_t cpu) {
    unsigned long mask[GLK43499_INTERNAL_CPU_MASK_BITS / (8U * sizeof(unsigned long))];
    memset(mask, 0, sizeof(mask));
    mask[cpu / (8U * sizeof(unsigned long))] |= (unsigned long) 1
        << (cpu % (8U * sizeof(unsigned long)));
    return (int32_t) syscall(SYS_sched_setaffinity, 0, (size_t) sizeof(mask),
                             mask);
}

/**
 * @brief 计算 2 的幂次对齐所需的 floor mask 宽度。
 * @param value 待验证值。
 * @return 1 表示 value 是非零 2 的幂；0 拒绝。
 */
static inline int32_t glk43499_internal_valid_pow2(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0 ? 1 : 0;
}

/**
 * @brief 判断无符号 64 位加法是否溢出。
 * @param left 左操作数。
 * @param right 右操作数。
 * @param out 非空的输出指针；溢出时不写。
 * @return 1 未溢出；0 溢出。
 * @note 阶段：所有物理地址/长度加法（TCP 映射地址、页偏移、边界组合）必须经此检查。
 */
static inline int32_t glk43499_internal_add_u64(uint64_t left, uint64_t right,
                                                uint64_t *out) {
    return __builtin_add_overflow(left, right, out) ? 0 : 1;
}

/**
 * @brief 判断 `指针 + 偏移` 是否溢出（arm64 上 uintptr_t 为 64 位）。
 * @param base 基址。
 * @param offset 64 位偏移；超出 uintptr_t 表示不可表示，直接拒绝。
 * @param out 非空的输出指针；失败时不写。
 * @return 1 未溢出；0 溢出/不可表示。
 * @note 阶段：Carrier 映射地址 = mapping_base + mapping_offset。
 */
static inline int32_t glk43499_internal_add_uintptr(uintptr_t base,
                                                    uint64_t offset,
                                                    uintptr_t *out) {
    if ((uint64_t) (uintptr_t) offset != offset) return 0;
    return __builtin_add_overflow(base, (uintptr_t) offset, out) ? 0 : 1;
}

/**
 * @brief 判断无符号乘法是否溢出。
 * @param left 左操作数。
 * @param right 右操作数。
 * @param out 非空的乘积输出。
 * @return 1 未溢出；0 溢出（不写 out）。
 */
static inline int32_t glk43499_internal_mul_u64(uint64_t left, uint64_t right,
                                                uint64_t *out) {
    if (left != 0 && right > UINT64_MAX / left) return 0;
    *out = left * right;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_CORE_INTERNAL_H */
