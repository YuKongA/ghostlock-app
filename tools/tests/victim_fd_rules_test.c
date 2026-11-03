/**
 * @file victim_fd_rules_test.c
 * @brief N02 spawn file_actions addclose 规则 host 单测（task-63）。
 *
 * 冻结规则（`victim_protocol.h:glk_victim_should_close_fd`）：
 * - 只 close 原 fd > 2 且 != control(3) && != raw(4)；
 * - 0/1/2 保留给 helper 输出/输入；3/4 是 dup2 安装目标，不得被 addclose。
 * 与 E4 真机 fixture（helper fd 集合恰为 {0,1,2,3,4}）配对。
 *
 * 编译/运行（macOS host）：
 *   clang -std=c11 -Wall -Wextra -Werror \
 *     -I app/src/main/cpp/backends/43499/core \
 *     tools/tests/victim_fd_rules_test.c -o /tmp/victim_fd_rules_test
 *   /tmp/victim_fd_rules_test
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "victim_protocol.h"

static int32_t g_failures = 0;

/**
 * @brief 断言 addclose 规则结果。
 * @param fd 父侧原 fd。
 * @param expected 期望（1=close）。
 */
static void expect_close(int32_t fd, int32_t expected) {
    const int32_t actual = glk_victim_should_close_fd(fd);
    if (actual != expected) {
        fprintf(stderr, "FAIL fd=%" PRId32 " expected=%" PRId32 " got=%" PRId32 "\n",
                fd, expected, actual);
        ++g_failures;
        return;
    }
    printf("PASS fd=%" PRId32 " close=%" PRId32 "\n", fd, actual);
}

int main(void) {
    /* 常量 ABI：control=3/raw=4。 */
    if (GLK_VICTIM_CONTROL_FD != 3 || GLK_VICTIM_RAW_FD != 4) {
        fprintf(stderr, "FAIL fd constants: control=%d raw=%d\n",
                GLK_VICTIM_CONTROL_FD, GLK_VICTIM_RAW_FD);
        return 1;
    }
    printf("PASS constants: control=%d raw=%d\n", GLK_VICTIM_CONTROL_FD,
           GLK_VICTIM_RAW_FD);
    /* 标准 fd 保留。 */
    expect_close(0, 0);
    expect_close(1, 0);
    expect_close(2, 0);
    /* dup2 目标 fd 不得 addclose。 */
    expect_close(3, 0);
    expect_close(4, 0);
    /* 其余父侧 fd 必须关闭（防继承泄漏）。 */
    expect_close(5, 1);
    expect_close(6, 1);
    expect_close(63, 1);
    expect_close(1023, 1);
    /* 负 fd / 非法输入不 close（调用方本就不会建 pipe）。 */
    expect_close(-1, 0);
    if (g_failures != 0) {
        fprintf(stderr, "victim_fd_rules_test: FAIL failures=%d\n", g_failures);
        return 1;
    }
    printf("victim_fd_rules_test: OK (保留 0..4；关闭 >2 且非 3/4)\n");
    return 0;
}
