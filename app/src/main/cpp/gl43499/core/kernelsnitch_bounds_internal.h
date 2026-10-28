/**
 * @file kernelsnitch_bounds_internal.h
 * @brief 43499 专属：KernelSnitch 扫描/identity 范围的单一权威派生。
 *
 * @note B7（task-81）：detector 的实际扫描范围由共享 header-only
 *       `glgeneral/kernelsnitch/kernelsnitch.h` 的编译期常量决定
 *       （`IDENTITY_START/END`、`COARSE_SZ`、`PAGE_SIZE`）；Kotlin 请求里的
 *       identity/scan 值**不驱动扫描**，只做事后校验。故 43499 在请求校验阶段
 *       用本头集中提取 detector 常量并做严格相等校验：不一致 ⇒ fail-closed。
 * @note 共享 `glkgeneral/kernelsnitch` 头不修改（避免与 98260 冲突）；真正
 *       detector 参数化（运行时范围）另立设计（shared header/TD）。
 * @note 本头不做任何分配/IO/业务判断；只把编译期常量映射为 uint64_t。
 */
#ifndef GHOSTLOCK_GL43499_CORE_KERNELSNITCH_BOUNDS_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_KERNELSNITCH_BOUNDS_INTERNAL_H

#include <stdint.h>

/* 唯一权威来源：detector 编译期常量（`__ARM`/PAGE_SIZE 由 target 编译定义提供）。 */
#include "kernelsnitch/kernelsnitch.h"

/** @brief detector identity 下界（`IDENTITY_START`）。 */
#define GLK43499_KERNELSNITCH_IDENTITY_START ((uint64_t) IDENTITY_START)
/** @brief detector identity 上界（`IDENTITY_END`；不含）。 */
#define GLK43499_KERNELSNITCH_IDENTITY_END ((uint64_t) IDENTITY_END)
/** @brief detector 粗扫步长（`COARSE_SZ`；仅登记/派生，不改变请求语义）。 */
#define GLK43499_KERNELSNITCH_COARSE_STEP ((uint64_t) COARSE_SZ)

/**
 * @brief scan 窗口下/上界。
 * @note detector 的 identity 范围即其唯一可扫描范围；scan 窗口必须与之一致。
 */
#define GLK43499_KERNELSNITCH_SCAN_LOWER_BOUND \
    GLK43499_KERNELSNITCH_IDENTITY_START
#define GLK43499_KERNELSNITCH_SCAN_UPPER_BOUND \
    GLK43499_KERNELSNITCH_IDENTITY_END
/**
 * @brief 请求 scan_step 的编译期期望值。
 * @note 请求的 scan_step 是最终候选页对齐粒度（Kotlin 0x1000）；与 detector
 *       的 `PAGE_SIZE` 编译期常量严格相等（不是 detector 内层 mm 扫描步长，
 *       后者由运行期 mm_struct_sz 派生，不能作为编译期常量）。
 */
#define GLK43499_KERNELSNITCH_SCAN_STEP ((uint64_t) PAGE_SIZE)

/** @brief 具名失败 token（回执 system_error 用 EINVAL；文本仅供测试/诊断）。 */
#define GLK43499_KERNELSNITCH_SCAN_BOUNDS_MISMATCH_REASON "scan-bounds-mismatch"

/** @brief `glk43499_placement_validate` 返回：通过。 */
#define GLK43499_PLACEMENT_VALIDATE_OK 1
/** @brief 返回：一般参数非法（既有 fail-closed）。 */
#define GLK43499_PLACEMENT_VALIDATE_INVALID 0
/** @brief 返回：request 范围与 detector 编译期常量不一致（B7）。 */
#define GLK43499_PLACEMENT_VALIDATE_SCAN_BOUNDS_MISMATCH 2

#endif /* GHOSTLOCK_GL43499_CORE_KERNELSNITCH_BOUNDS_INTERNAL_H */
