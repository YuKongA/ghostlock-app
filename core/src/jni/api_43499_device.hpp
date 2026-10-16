#pragma once
/** @file jni/api_43499_device.hpp
 *  43499 的设备侧原语（薄）：按 id 查表 + 调一个既有原语 + 回来。
 *
 *  拼装、写目标、收尾都在 Kotlin（Assembled43499）；native 只提供原语。
 */
#include <cstdint>
#include <vector>
#include "backend/cve_2026_43499/access_params.hpp"

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
/** Fill from the assembly (new; the four-argument version stays). */
void fill_access_params_43499_full(backend::cve_2026_43499::Access43499Parameters &params,
                                 std::uint64_t route, std::uint64_t offset,
                                 bool offset_set, const std::uint64_t *assembly,
                                 std::size_t count) noexcept;

/** Initialize carrying the assembly (new entry point). */
[[nodiscard]] int device_initialize_43499_full(std::uint64_t accessId, std::uint64_t route,
                                            std::uint64_t offset, bool offset_set,
                                            const std::uint64_t *assembly, std::size_t count) noexcept;
    [[nodiscard]] int device_shutdown_43499(std::uint64_t accessId) noexcept;
    /** @return 拆掉一个 43499 的内核访问对象。 */
    [[nodiscard]] bool device_destroy_access_43499(std::uint64_t accessId) noexcept;
/** Adapter context: which access and which route an action applies to. */
    struct Ops43499Context final { std::uint64_t access_id = 0U; std::uint64_t route = 0U; };

/** Device action table for 43499 (forward declared; defined in api_43499_ops.cpp). */
    [[nodiscard]] struct DeviceOps43499 device_ops_43499(Ops43499Context *context) noexcept;

    /** @return 设备动作表的上下文（与 access 表并排；make_access 时盖戳）。 */
    [[nodiscard]] Ops43499Context *current_ops_ctx_43499() noexcept;

}  // namespace ghostlock::jni
