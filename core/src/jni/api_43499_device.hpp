#pragma once
/** @file jni/api_43499_device.hpp
 *  43499 的设备侧原语（薄）：按 id 查表 + 调一个既有原语 + 回来。
 *
 *  拼装、写目标、收尾都在 Kotlin（Assembled43499）；native 只提供原语。
 */
#include <cstdint>
#include <vector>

namespace ghostlock::jni {
    /**
     *  按 route 建一个内核访问对象（内部走 create_kernel_access_43499）。
     *
     *  @param route 档案里的 route 取值（profile::RouteId 的整数形式）。
     *  @return 不透明 id（slot + 1）；route 非法或表满 ⇒ 0。
     */
    [[nodiscard]] std::uint64_t device_make_access_43499(int route) noexcept;

    /** 写一块（通道 PiWindow）。 */
    [[nodiscard]] bool device_write_43499(std::uint64_t accessId, std::uint64_t route,
                                        std::uint64_t offset, std::uint64_t address,
                                        const std::vector<std::uint8_t> &block,
                                        const char **why) noexcept;
    /** 读一块（通道 PiWindow）。 */
    [[nodiscard]] bool device_read_43499(std::uint64_t accessId, std::uint64_t route,
                                       std::uint64_t offset, std::uint64_t address,
                                       std::size_t bytes, std::vector<std::uint8_t> &out,
                                       const char **why) noexcept;
    /** 初始化（路由与位置由上层给）。 */
    [[nodiscard]] int device_initialize_43499(std::uint64_t accessId, std::uint64_t route,
                                             std::uint64_t offset, bool offset_set) noexcept;
    /** @return 0 成功；否则 Access 状态码（收尾第一步，先 shutdown 再 destroy）。 */
    [[nodiscard]] int device_shutdown_43499(std::uint64_t accessId) noexcept;
    /** @return 拆掉一个 43499 的内核访问对象。 */
    [[nodiscard]] bool device_destroy_access_43499(std::uint64_t accessId) noexcept;
}  // namespace ghostlock::jni
