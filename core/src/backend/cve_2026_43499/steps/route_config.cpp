/** @file backend/cve_2026_43499/steps/route_config.cpp
 *  The pure build. Every required field is checked here so a half-filled wire never reaches a
 *  thread: the refusal names the field the caller forgot.
 */
#include "backend/cve_2026_43499/steps/route_config.hpp"

#include <limits>

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        /** 一个必须非零的字段。@return true 当它可以被用；否则写拒绝名。 */
        [[nodiscard]] bool needed(std::uint64_t value, const char *field, RouteConfigOutcome &out) noexcept {
            if (value != 0U) {
                return true;
            }
            static constexpr const char *kMissing = "route-config-missing:";
            /* 名字拼在**静态**缓冲里：拒绝名要被设备侧打印，不能指向栈。 */
            static char named[96] = {};
            std::size_t at = 0U;
            for (const char *c = kMissing; *c != '\0' && at + 1U < sizeof(named); ++c) { named[at++] = *c; }
            for (const char *c = field; *c != '\0' && at + 1U < sizeof(named); ++c) { named[at++] = *c; }
            named[at] = '\0';
            out.refusal = named;
            return false;
        }

        [[nodiscard]] bool fits_i32(std::uint64_t value, const char *field, RouteConfigOutcome &out) noexcept {
            if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                return true;
            }
            static constexpr const char *kTooBig = "route-config-too-big:";
            static char named[96] = {};
            std::size_t at = 0U;
            for (const char *c = kTooBig; *c != '\0' && at + 1U < sizeof(named); ++c) { named[at++] = *c; }
            for (const char *c = field; *c != '\0' && at + 1U < sizeof(named); ++c) { named[at++] = *c; }
            named[at] = '\0';
            out.refusal = named;
            return false;
        }
    }  // namespace

    RouteConfigOutcome build_route_config(const RouteConfigValues &values) noexcept {
        RouteConfigOutcome out{};
        /* 几何四件：位移可以合法地为 0（落在 buffer 开头），所以只校验"整体齐不齐"由 buffer_size 与位移的**
         *  相互关系**决定——这里只要求 buffer_size 非零，位移的合法性交给单元自己的判据（`ArmVerdict`）。 */
        if (!needed(values.buffer_size, "buffer_size", out)) { return out; }
        if (!needed(values.max_attempts, "max_attempts", out)) { return out; }
        if (!needed(values.arm_sequence, "arm_sequence", out)) { return out; }
        if (!needed(values.arm_hold, "arm_hold", out)) { return out; }
        if (!needed(values.park_ms, "park_ms", out)) { return out; }
        if (!needed(values.settle_us, "settle_us", out)) { return out; }
        /* delay_us 允许为 0（多播分支退役里就是 0），其余消费位必须非零。 */
        if (!needed(values.burst_calls, "burst_calls", out)) { return out; }
        if (!needed(values.max_calls, "max_calls", out)) { return out; }
        if (!needed(values.fallback_lock_us, "fallback_lock_us", out)) { return out; }
        if (!needed(values.nice_modulus, "nice_modulus", out)) { return out; }
        if (!needed(values.target, "target", out)) { return out; }
        /* 实验（round 253 ✓，TODO(exp)：如通过 ⇒ 按 docs/analysis/43499-zero-value-confirmed.md 的修法 ① 定稿 ✓）：
         *  旧码：if (!needed(values.value, "value", out)) { return out; }
         *  `Zero` 模式的写值**合法地就是 0** ✗ ⇒ 旧校验把"0"与"缺失"混为一谈 ⇒ `w3` 报
         *  `route-config-missing:value` ✗（真机三步读数证实 ✓）⇒ 此处**允许 0** ✓。 */
        if (!needed(values.fake_task, "fake_task", out)) { return out; }
        if (!needed(values.fake_lock, "fake_lock", out)) { return out; }
        if (!fits_i32(values.max_attempts, "max_attempts", out)) { return out; }
        if (!fits_i32(values.arm_sequence, "arm_sequence", out)) { return out; }
        if (!fits_i32(values.arm_hold, "arm_hold", out)) { return out; }
        if (!fits_i32(values.park_ms, "park_ms", out)) { return out; }
        if (!fits_i32(values.settle_us, "settle_us", out)) { return out; }
        if (!fits_i32(values.consumer_delay_us, "consumer_delay_us", out)) { return out; }
        if (!fits_i32(values.burst_calls, "burst_calls", out)) { return out; }
        if (!fits_i32(values.max_calls, "max_calls", out)) { return out; }
        if (!fits_i32(values.fallback_lock_us, "fallback_lock_us", out)) { return out; }
        if (!fits_i32(values.nice_modulus, "nice_modulus", out)) { return out; }
        landing::MulticastRouteConfig config{};
        config.stamp.waiter_offset = static_cast<std::size_t>(values.waiter_offset);
        config.stamp.task_offset = static_cast<std::size_t>(values.task_offset);
        config.stamp.lock_offset = static_cast<std::size_t>(values.lock_offset);
        config.stamp.buffer_size = static_cast<std::size_t>(values.buffer_size);
        config.limits.max_attempts = static_cast<std::int32_t>(values.max_attempts);
        config.limits.arm_hold = static_cast<std::int32_t>(values.arm_hold);
        config.limits.arm_sequence = static_cast<std::int32_t>(values.arm_sequence);
        config.park_ms = static_cast<std::int32_t>(values.park_ms);
        config.settle_us = static_cast<std::int32_t>(values.settle_us);
        /* ★ 更正（第 119/120 轮 ✓）：`config.settle_us`（上一行 ✓）**就是** requeue 之前的 settle ✓，
         *  它一直在用（`multicast_route.hpp` 的 `requeue()` 里 ✓）⇒ ⇒ **⇒ 第 109/111 轮的"接上"是重复件** ✗ ⇒ 已撤回 ✓。 */
        config.main_cpu = static_cast<std::int32_t>(values.main_cpu);
        config.consumer_cpu = static_cast<std::int32_t>(values.consumer_cpu);
        /* 两个相位可以为 0（= 调用方明确说"不等"），故只查范围、不查"必需"。 */
        if (!fits_i32(values.post_requeue_settle_us, "post_requeue_settle_us", out)) { return out; }
        if (!fits_i32(values.post_adjust_settle_us, "post_adjust_settle_us", out)) { return out; }
        config.post_requeue_settle_us = static_cast<std::int32_t>(values.post_requeue_settle_us);
        config.post_adjust_settle_us = static_cast<std::int32_t>(values.post_adjust_settle_us);
        config.consumer_delay_us = static_cast<std::int32_t>(values.consumer_delay_us);
        config.burst_calls = static_cast<std::int32_t>(values.burst_calls);
        config.max_calls = static_cast<std::int32_t>(values.max_calls);
        config.fallback_lock_us = static_cast<std::int32_t>(values.fallback_lock_us);
        config.nice_modulus = static_cast<std::int32_t>(values.nice_modulus);
        config.policy = static_cast<std::uint32_t>(values.policy);
        config.target = values.target;
        config.value = values.value;
        config.fake_task = values.fake_task;
        config.fake_lock = values.fake_lock;
        out.config = config;
        return out;
    }

    const char *route_config_refusal_name(const RouteConfigOutcome &outcome) noexcept {
        return outcome.refusal;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
