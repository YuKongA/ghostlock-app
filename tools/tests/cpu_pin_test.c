/**
 * @file cpu_pin_test.c
 * @brief G08 Actor CPU pin 哨兵 host 单测（task-64）。
 *
 * 只包含纯头 `cpu_pin_internal.h`，对拍：
 * - 哨兵 0xFFFFFFFF（Kotlin -1 u32）⇒ owner/waiter 不 pin；
 * - 普通 CPU 0/1/2/31/4095 ⇒ 需要 pin；
 * - consumer 走既有 valid_cpu（其校验由 actor.c 负责；本测只覆盖纯判定）。
 *
 * 编译/运行：
 *   clang -std=c11 -Wall -Wextra -Werror \
 *     -I app/src/main/cpp/backends/43499/core tools/tests/cpu_pin_test.c \
 *     -o /tmp/cpu_pin_test && /tmp/cpu_pin_test
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu_pin_internal.h"

static int32_t g_failures = 0;

/**
 * @brief 断言 pin 判定。
 * @param cpu CPU 域。
 * @param expected_pin_required 期望（1=pin）。
 * @param expected_no_pin 期望（1=哨兵）。
 */
static void expect_cpu(uint32_t cpu, int32_t expected_pin_required,
                       int32_t expected_no_pin) {
    const int32_t required = glk43499_internal_cpu_pin_required(cpu);
    const int32_t no_pin = glk43499_internal_cpu_is_no_pin(cpu);
    if (required != expected_pin_required || no_pin != expected_no_pin) {
        fprintf(stderr,
                "FAIL cpu=%" PRIu32 " expected_required=%" PRId32
                " got_required=%" PRId32 " expected_no_pin=%" PRId32
                " got_no_pin=%" PRId32 "\n",
                cpu, expected_pin_required, required, expected_no_pin, no_pin);
        ++g_failures;
        return;
    }
    printf("PASS cpu=%" PRIu32 " pin_required=%" PRId32 " no_pin=%" PRId32 "\n",
           cpu, required, no_pin);
}

int main(void) {
    /* Kotlin -1 ⇒ JNI get_int 转 u32 = 0xFFFFFFFF = no-pin 哨兵。 */
    expect_cpu(UINT32_C(0xFFFFFFFF), 0, 1);
    /* 普通 CPU（legacy 0/1/2 与上界样例）需要 pin。 */
    expect_cpu(UINT32_C(0), 1, 0);
    expect_cpu(UINT32_C(1), 1, 0);
    expect_cpu(UINT32_C(2), 1, 0);
    expect_cpu(UINT32_C(31), 1, 0);
    expect_cpu(UINT32_C(4095), 1, 0);
    if (g_failures != 0) {
        fprintf(stderr, "cpu_pin_test: FAIL failures=%d\n", g_failures);
        return 1;
    }
    printf("cpu_pin_test: OK (0xFFFFFFFF=no-pin; others pin)\n");
    return 0;
}
