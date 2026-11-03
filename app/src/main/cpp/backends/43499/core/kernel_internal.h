/**
 * @file kernel_internal.h
 * @brief 43499 内部实现常量占位（ABI v4 / A 决策后**不再保留任何数值宏**）。
 *
 * @note 用户裁定（2026-10-27「A」）：`GLK43499_PHYS_*` / `GLK43499_STRUCT_*` /
 *       `GLK43499_WIRE_*` 目标/容量/协议数值宏全部删除，改为公共 ABI 请求字段
 *       （见 `placement.h`/`primitive.h`）并由 fail-closed 校验消费。
 * @note 本文件保留为 include 锚点，便于内外私有头统一依赖关系；**禁止**在此
 *       重新引入任何目标参数、容量或 wire 协议数值宏。编译期常量只允许
 *       `sizeof` 派生的 ABI 布局断言（在各 ABI 头内）。
 * @note 阶段：R2b（43499 Native 全参数化）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H
#define GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H

#include <stdint.h>

#include "abi.h"

#endif /* GHOSTLOCK_GL43499_CORE_KERNEL_INTERNAL_H */
