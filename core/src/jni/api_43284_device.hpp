#pragma once
/** @file jni/api_43284_device.hpp
 *  本构建能提供的 43284 缝（数据性的那些：两张图与钩子计划）。
 */
#include "jni/api_43284.hpp"

#include "backend/cve_2026_43284/access_params.hpp"




namespace ghostlock::jni {
    /**
     *  会话参数 → 门的材料：spi / 端口 / ICV / 两个 32 字节密钥。
     *
     *  密钥长度不对就没有材料（set = false）—— 零密钥不是凭据，门的开启会据此拒绝，
     *  而不是拿一段零去连。
     */
    [[nodiscard]] ghostlock::backend::cve_2026_43284::SaMaterial sa_material_of(
        const Init43284Values &params) noexcept;

    /** @return 本构建的 43284 缝；其余（门、块读写、模块校验、触发、等待）留空 ⇒ op-not-wired。 */
    [[nodiscard]] DeviceOps43284 device_ops_43284() noexcept;
}  // namespace ghostlock::jni
