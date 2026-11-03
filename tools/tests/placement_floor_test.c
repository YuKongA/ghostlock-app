/**
 * @file placement_floor_test.c
 * @brief 43499 Placement legacy-floor 对象数派生 host 单测（task-58）。
 *
 * 只包含公开的 `placement.h`（无 Linux/JNI 依赖），直接对拍
 * `glk43499_placement_objects_per_slab` 与 A301SO/mondrian 设备事实：
 * - A301SO 5.15：allocation=32768、mm_struct=1024 ⇒ 32（整除不变）；
 * - mondrian 5.10：allocation=32768、mm_struct=960 ⇒ 34（floor，余 128）；
 * - 边界：mm>allocation、0、结果 0、结果 >UINT32_MAX、精确整除、非整除 floor。
 *
 * 编译/运行（macOS host，无 Android 依赖）：
 *   clang -std=c11 -Wall -Wextra -Werror \
 *     -I app/src/main/cpp/backends/43499/core \
 *     tools/tests/placement_floor_test.c -o /tmp/placement_floor_test
 *   /tmp/placement_floor_test
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "placement.h"

static int32_t g_failures = 0;

/**
 * @brief 断言一次派生结果。
 * @param allocation_size 输入 allocation。
 * @param mm_struct_size 输入 mm_struct。
 * @param expected_ok 期望是否成功。
 * @param expected_objects 成功时期望的 objects。
 * @param label 用例标签。
 */
static void expect_objects(uint64_t allocation_size, uint64_t mm_struct_size,
                           int32_t expected_ok, uint32_t expected_objects,
                           const char *label) {
    uint32_t objects = UINT32_C(0xdeadbeef);
    const int32_t ok = glk43499_placement_objects_per_slab(
        allocation_size, mm_struct_size, &objects);
    if (ok != expected_ok ||
        (expected_ok != 0 && objects != expected_objects)) {
        fprintf(stderr,
                "FAIL %s: allocation=%" PRIu64 " mm=%" PRIu64
                " expected_ok=%" PRId32 " got_ok=%" PRId32
                " expected_objects=%" PRIu32 " got_objects=%" PRIu32 "\n",
                label, allocation_size, mm_struct_size, expected_ok, ok,
                expected_objects, objects);
        ++g_failures;
        return;
    }
    printf("PASS %s: allocation=%" PRIu64 " mm=%" PRIu64
           " objects=%" PRIu32 "\n",
           label, allocation_size, mm_struct_size, objects);
}

int main(void) {
    /* A301SO 5.15：1024 | 32768，整除路径数值不变。 */
    expect_objects(UINT64_C(32768), UINT64_C(1024), 1, UINT32_C(32),
                   "a301so_5_15");
    /* mondrian 5.10：960 非整除 32768 ⇒ floor=34（余 128）。 */
    expect_objects(UINT64_C(32768), UINT64_C(960), 1, UINT32_C(34),
                   "mondrian_5_10_floor");
    /* 精确整除边界与 1 对象。 */
    expect_objects(UINT64_C(960), UINT64_C(960), 1, UINT32_C(1),
                   "exact_one_object");
    expect_objects(UINT64_C(32768), UINT64_C(1025), 1, UINT32_C(31),
                   "non_pow2_floor");
    expect_objects(UINT64_MAX, UINT64_MAX, 1, UINT32_C(1),
                   "one_object_max");
    /* mm > allocation / 0 输入 fail-closed。 */
    expect_objects(UINT64_C(959), UINT64_C(960), 0, UINT32_C(0),
                   "mm_gt_allocation");
    expect_objects(UINT64_C(0), UINT64_C(960), 0, UINT32_C(0),
                   "zero_allocation");
    expect_objects(UINT64_C(32768), UINT64_C(0), 0, UINT32_C(0),
                   "zero_mm");
    expect_objects(UINT64_C(1), UINT64_C(2), 0, UINT32_C(0),
                   "allocation_lt_mm");
    /* 结果域：最大可表示 u32；超出即拒绝（Native 请求字段为 u32）。 */
    expect_objects(UINT64_MAX, UINT64_C(1), 0, UINT32_C(0),
                   "objects_overflow");
    expect_objects(UINT64_C(0x100000000), UINT64_C(1), 0, UINT32_C(0),
                   "objects_2_pow_32");
    expect_objects(UINT64_C(0xffffffff), UINT64_C(1), 1, UINT32_C(0xffffffff),
                   "objects_u32_max");
    /* 空输出指针必须拒绝。 */
    if (glk43499_placement_objects_per_slab(UINT64_C(32768),
                                            UINT64_C(1024), NULL) != 0) {
        fprintf(stderr, "FAIL null_out: expected rejection\n");
        ++g_failures;
    } else {
        printf("PASS null_out: rejected\n");
    }
    if (g_failures != 0) {
        fprintf(stderr, "placement_floor_test: FAIL failures=%d\n", g_failures);
        return 1;
    }
    printf("placement_floor_test: OK (A301SO=32, mondrian=34, boundaries)\n");
    return 0;
}
