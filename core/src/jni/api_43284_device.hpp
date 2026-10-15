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
    /** @return ESP socket fd（发送端绑 sender_port、连 encap_port）；失败 -1。 */
    [[nodiscard]] int device_connect_esp(const Init43284Values &params) noexcept;
    /** @return 关掉一个 socket；fd < 0 则为 false。 */
    [[nodiscard]] bool device_close_socket(int fd) noexcept;

    /** @return 用会话参数的材料把落地对象拉起来。 */
    [[nodiscard]] bool device_bring_up(std::uint64_t landingId, const Init43284Values &params) noexcept;
    /** @return 窗口的下一个序号；id 无效 ⇒ 0。 */
    [[nodiscard]] std::uint64_t device_next_sequence(std::uint64_t landingId) noexcept;
    /** @return 让落地对象采用下一个序号。 */
    [[nodiscard]] bool device_adopt_sequence(std::uint64_t landingId, std::uint64_t next) noexcept;

    /** @return 建 socket 并存表；失败 0。 */
    [[nodiscard]] std::uint64_t device_esp_socket_id(const Init43284Values &params) noexcept;
    /** @return 按 id 关掉 socket。 */
    [[nodiscard]] bool device_close_socket_id(std::uint64_t socketId) noexcept;
    /** @return 建拼接面（薄门 + 真拼接面）；失败 0。 */
    [[nodiscard]] std::uint64_t device_make_surface(std::uint64_t targetId, std::uint64_t socketId) noexcept;
    /** @return 拆掉拼接面（连它的落地对象一起）。 */
    [[nodiscard]] bool device_destroy_surface(std::uint64_t surfaceId) noexcept;
    /** @return 按拼接面建落地对象；失败 0。 */
    [[nodiscard]] std::uint64_t device_make_landing(std::uint64_t surfaceId) noexcept;
    /** @return 拆掉落地对象。 */
    [[nodiscard]] bool device_destroy_landing(std::uint64_t landingId) noexcept;

    /** @return 关掉一扇门；id 无效或门本来没开则为 false。 */
    [[nodiscard]] bool device_close_target(std::uint64_t targetId) noexcept;

    [[nodiscard]] ghostlock::backend::cve_2026_43284::SaMaterial sa_material_of(
        const Init43284Values &params) noexcept;

    /** @return 本构建的 43284 缝；其余（门、块读写、模块校验、触发、等待）留空 ⇒ op-not-wired。 */
    [[nodiscard]] DeviceOps43284 device_ops_43284() noexcept;
}  // namespace ghostlock::jni
