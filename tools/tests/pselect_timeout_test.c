/**
 * @file pselect_timeout_test.c
 * @brief G02 Pselect 每 attempt 超时派生 host 单测（task-63）。
 *
 * 只包含纯头 `pselect_timeout_internal.h`（无 Android/Linux 依赖），对拍：
 * - 5.10 legacy 等价：800ms/4 attempts ⇒ 200ms/attempt；
 * - 边界：budget=1 ⇒ 800ms；budget=0 ⇒ 按 1 次 ⇒ 800ms；timeout<budget ⇒ floor 0；
 * - attempt_timeout=min(remaining, per_attempt) 的截断/边界。
 *
 * 编译/运行（macOS host）：
 *   clang -std=c11 -Wall -Wextra -Werror \
 *     -I app/src/main/cpp/backends/43499/core \
 *     tools/tests/pselect_timeout_test.c -o /tmp/pselect_timeout_test
 *   /tmp/pselect_timeout_test
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "pselect_timeout_internal.h"

static int32_t g_failures = 0;

/**
 * @brief 断言派生结果。
 * @param timeout_ns 总窗口。
 * @param budget attempts。
 * @param expected 期望 per-attempt。
 * @param label 用例。
 */
static void expect_per_attempt(uint64_t timeout_ns, uint64_t budget,
                               uint64_t expected, const char *label) {
    const uint64_t actual =
        glk43499_pselect_per_attempt_timeout_ns(timeout_ns, budget);
    if (actual != expected) {
        fprintf(stderr,
                "FAIL %s: timeout=%" PRIu64 " budget=%" PRIu64
                " expected=%" PRIu64 " got=%" PRIu64 "\n",
                label, timeout_ns, budget, expected, actual);
        ++g_failures;
        return;
    }
    printf("PASS %s: timeout=%" PRIu64 " budget=%" PRIu64
           " per_attempt=%" PRIu64 "\n",
           label, timeout_ns, budget, actual);
}

/**
 * @brief 断言 min(remaining, per_attempt) 截断。
 * @param remaining 剩余总窗口。
 * @param per_attempt 单次预算。
 * @param expected 期望实际超时。
 * @param label 用例。
 */
static void expect_attempt(uint64_t remaining, uint64_t per_attempt,
                           uint64_t expected, const char *label) {
    const uint64_t actual =
        glk43499_pselect_attempt_timeout_ns(remaining, per_attempt);
    if (actual != expected) {
        fprintf(stderr,
                "FAIL %s: remaining=%" PRIu64 " per_attempt=%" PRIu64
                " expected=%" PRIu64 " got=%" PRIu64 "\n",
                label, remaining, per_attempt, expected, actual);
        ++g_failures;
        return;
    }
    printf("PASS %s: remaining=%" PRIu64 " per_attempt=%" PRIu64
           " attempt_timeout=%" PRIu64 "\n",
           label, remaining, per_attempt, actual);
}

int main(void) {
    const uint64_t ms = UINT64_C(1000000);
    /* 5.10 legacy 语义：800ms / 4 attempts = 4 × 200ms。 */
    expect_per_attempt(UINT64_C(800) * ms, UINT64_C(4),
                       UINT64_C(200) * ms, "5_10_800ms_4_attempts");
    /* budget=1 ⇒ 整窗一次。 */
    expect_per_attempt(UINT64_C(800) * ms, UINT64_C(1),
                       UINT64_C(800) * ms, "budget_1");
    /* budget=0 按 1 次尝试处理（除零保护）。 */
    expect_per_attempt(UINT64_C(800) * ms, UINT64_C(0),
                       UINT64_C(800) * ms, "budget_0_as_1");
    /* 5000ms/2000（模板 operation_budget 更大时）⇒ 2.5ms floor。 */
    expect_per_attempt(UINT64_C(5000) * ms, UINT64_C(2000),
                       UINT64_C(2500) * 1000, "floor_non_divisible");
    /* timeout<budget ⇒ 整数下取整 0（select 非阻塞；循环仍受 attempts/deadline）。 */
    expect_per_attempt(UINT64_C(3), UINT64_C(4), UINT64_C(0),
                       "timeout_lt_budget_floor_0");
    expect_per_attempt(UINT64_C(0), UINT64_C(4), UINT64_C(0), "zero_timeout");
    /* 大数溢出安全：UINT64_MAX/1 = UINT64_MAX。 */
    expect_per_attempt(UINT64_MAX, UINT64_C(1), UINT64_MAX, "max_budget_1");

    /* min(remaining, per_attempt)：总窗口边界。 */
    expect_attempt(UINT64_C(900) * ms, UINT64_C(200) * ms,
                   UINT64_C(200) * ms, "remaining_gt_per_attempt");
    expect_attempt(UINT64_C(50) * ms, UINT64_C(200) * ms,
                   UINT64_C(50) * ms, "remaining_lt_per_attempt");
    expect_attempt(UINT64_C(200) * ms, UINT64_C(200) * ms,
                   UINT64_C(200) * ms, "remaining_eq_per_attempt");
    expect_attempt(UINT64_C(0), UINT64_C(200) * ms, UINT64_C(0),
                   "remaining_0");
    if (g_failures != 0) {
        fprintf(stderr, "pselect_timeout_test: FAIL failures=%d\n", g_failures);
        return 1;
    }
    printf("pselect_timeout_test: OK (per_attempt=min(remaining, timeout/budget))\n");
    return 0;
}
