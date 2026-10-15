#pragma once
/** @file jni/api_43284_device.hpp
 *  本构建能提供的 43284 缝（数据性的那些：两张图与钩子计划）。
 */
#include "jni/api_43284.hpp"

namespace ghostlock::jni {
    /** @return 本构建的 43284 缝；其余（门、块读写、模块校验、触发、等待）留空 ⇒ op-not-wired。 */
    [[nodiscard]] DeviceOps43284 device_ops_43284() noexcept;
}  // namespace ghostlock::jni
