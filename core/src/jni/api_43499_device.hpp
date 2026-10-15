#pragma once
/** @file jni/api_43499_device.hpp
 *  43499 的设备侧原语（薄）：按 id 查表 + 调一个既有原语 + 回来。
 *
 *  拼装、写目标、收尾都在 Kotlin（Assembled43499）；native 只提供原语。
 */
#include <cstdint>

namespace ghostlock::jni {
    /**
     *  按 route 建一个内核访问对象（内部走 create_kernel_access_43499）。
     *
     *  @param route 档案里的 route 取值（profile::RouteId 的整数形式）。
     *  @return 不透明 id（slot + 1）；route 非法或表满 ⇒ 0。
     */
    [[nodiscard]] std::uint64_t device_make_access_43499(int route) noexcept;

    /** @return 拆掉一个 43499 的内核访问对象。 */
    [[nodiscard]] bool device_destroy_access_43499(std::uint64_t accessId) noexcept;
}  // namespace ghostlock::jni
