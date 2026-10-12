#pragma once
/** @file backend/cve_2026_43499/steps/window_steps.hpp
 *  43499 链的封装步骤：三个竞态窗口阶段，脚本侧按名字请求（schema 的
 *  backend.cve_2026_43499.steps 声明 w1 / w2 / w3）。
 *
 *  边界：
 *    - 本单元只做「拿窗口」的节奏：尝试、让位、失败后的修复与链轮；
 *    - 「怎么尝试一次」是注入的（设备上是真竞态，测试上是假的）——所以这套逻辑在主机上可测；
 *    - 失败一律具名：尝试用尽、钩子缺失都不是静默的零。
 */
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43499::steps {
    /** The three window stages, in the order the chain runs them. */
    enum class WindowStep : unsigned char { W1 = 0, W2 = 1, W3 = 2 };

    inline constexpr const char *kWindowStepNames[] = {"w1", "w2", "w3"};
    inline constexpr std::size_t kWindowStepCount = sizeof(kWindowStepNames) / sizeof(kWindowStepNames[0]);

    /** @return the stage a name denotes, or false when the name is not one of the three. */
    [[nodiscard]] bool window_step_by_name(std::string_view name, WindowStep &out) noexcept;
    /** @return the name of a stage (never null). */
    [[nodiscard]] const char *window_step_name(WindowStep step) noexcept;

    /** What one stage is given. The attempt hook is the device"s own race; the settle hook is its
     *  clock; the repair hook exists only for w1 (its scratch_repair_attempts). */
    struct WindowStageRequest final {
        std::uint32_t attempts = 0U;
        std::uint32_t settle_us = 0U;
        std::uint32_t scratch_repair_attempts = 0U;
        std::uint32_t chain_rounds = 0U;
        /** Try once. @return true when the window was won this attempt. */
        bool (*attempt)(void *context) = nullptr;
        /** Wait between attempts, in microseconds. */
        void (*settle)(std::uint32_t us) = nullptr;
        /** Repair the scratch; required only when the stage was given repair attempts. */
        bool (*repair_scratch)(void *context) = nullptr;
        void *context = nullptr;
    };

    /** Run one window stage: w1 and w2 try @p attempts times with @p settle_us between them, w1
     *  repairing the scratch when it is given repair attempts; w3 runs @p chain_rounds of that.
     *  @param step the stage
     *  @param request its numbers and hooks
     *  @return nullptr when the stage won, or a named reason (never a silent zero). */
    [[nodiscard]] const char *run_window_stage(WindowStep step, WindowStageRequest &request) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
