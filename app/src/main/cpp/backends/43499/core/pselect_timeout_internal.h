/**
 * @file pselect_timeout_internal.h
 * @brief Pselect 每 attempt 超时派生（G02 parity；内部 helper，不是 ABI）。
 *
 * 语义（legacy2 `route_select.cpp:457-470`）：一次 route 的总窗口 `timeout_ns` 要在
 * `operation_budget` 次 attempt 之间分配；旧实现每次 select 都拿剩余总 deadline 当
 * 超时 ⇒ 首次 select 就吃掉整个窗口、operation_budget 形同虚设（task-61 矩阵 G02）。
 *
 * 冻结公式（不新增请求字段）：
 * ```
 * per_attempt = timeout_ns / max(1, operation_budget)   // 800ms/4 = 200ms
 * attempt_timeout = min(remaining, per_attempt)         // 不超过总 deadline
 * ```
 * 边界：`operation_budget=0` 按 1 次尝试处理（per_attempt=timeout_ns）；整数除法
 * 先下取整（timeout_ns<budget 时 per_attempt=0 ⇒ select 非阻塞，循环仍受 attempts/
 * 总 deadline 约束）；调用方继续循环直到 attempts 用尽/total deadline/成功。
 *
 * @note 纯函数、无分配、无系统调用；宿主单测 `tools/tests/pselect_timeout_test.c`。
 * @note 阶段：P1a parity（task-63）；不改 `glk43499_pselect_spec` ABI。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PSELECT_TIMEOUT_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_PSELECT_TIMEOUT_INTERNAL_H

#include <stdint.h>

/**
 * @brief 派生单次 attempt 的 select 超时预算。
 *
 * @param timeout_ns route 总窗口（纳秒）。
 * @param operation_budget attempts 数（0 按 1 处理）。
 * @return per-attempt 预算（纳秒；整数除法下取整）。
 */
static inline uint64_t glk43499_pselect_per_attempt_timeout_ns(
    uint64_t timeout_ns, uint64_t operation_budget) {
    const uint64_t attempts = operation_budget == 0 ? 1 : operation_budget;
    return timeout_ns / attempts;
}

/**
 * @brief 计算本次 select 的实际超时：不超过剩余总窗口与 per-attempt 预算。
 *
 * @param remaining_ns 距 route 总 deadline 的剩余（纳秒）。
 * @param per_attempt_ns 单次预算（纳秒）。
 * @return min(remaining_ns, per_attempt_ns)。
 */
static inline uint64_t glk43499_pselect_attempt_timeout_ns(
    uint64_t remaining_ns, uint64_t per_attempt_ns) {
    return remaining_ns < per_attempt_ns ? remaining_ns : per_attempt_ns;
}

#endif /* GHOSTLOCK_GL43499_CORE_PSELECT_TIMEOUT_INTERNAL_H */
