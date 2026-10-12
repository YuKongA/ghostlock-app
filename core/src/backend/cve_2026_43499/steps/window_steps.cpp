/** @file backend/cve_2026_43499/steps/window_steps.cpp
 *  The window stages" rhythm: attempt, settle, repair (w1), chain (w3). See the header.
 */
#include "backend/cve_2026_43499/steps/window_steps.hpp"

#include <cstring>

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        inline constexpr const char *kAttemptMissing = "step-attempt-missing";
        inline constexpr const char *kAttemptsZero = "step-attempts-zero";
        inline constexpr const char *kRepairMissing = "step-repair-missing";
        inline constexpr const char *kNotWon = "step-window-not-won";
        inline constexpr const char *kRoundsZero = "step-rounds-zero";
    }  // namespace

    bool window_step_by_name(std::string_view name, WindowStep &out) noexcept {
        for (std::size_t i = 0U; i < kWindowStepCount; ++i) {
            if (std::string_view(kWindowStepNames[i]) == name) {
                out = static_cast<WindowStep>(i);
                return true;
            }
        }
        return false;
    }

    const char *window_step_name(WindowStep step) noexcept {
        const auto index = static_cast<std::size_t>(step);
        return index < kWindowStepCount ? kWindowStepNames[index] : "unknown";
    }

    namespace {
        /** One round of attempts, with settle between them. @return true when the window was won. */
        bool run_attempts(const WindowStageRequest &request) noexcept {
            for (std::uint32_t attempt = 0U; attempt < request.attempts; ++attempt) {
                if (request.attempt(request.context)) { return true; }
                if (attempt + 1U < request.attempts && request.settle != nullptr) {
                    request.settle(request.settle_us);
                }
            }
            return false;
        }
    }  // namespace

    const char *run_window_stage(WindowStep step, WindowStageRequest &request) noexcept {
        if (request.attempt == nullptr) { return kAttemptMissing; }
        if (request.attempts == 0U) { return kAttemptsZero; }
        if (step == WindowStep::W1 && request.scratch_repair_attempts != 0U &&
            request.repair_scratch == nullptr) {
            return kRepairMissing;
        }
        if (step == WindowStep::W3) {
            if (request.chain_rounds == 0U) { return kRoundsZero; }
            for (std::uint32_t round = 0U; round < request.chain_rounds; ++round) {
                if (!run_attempts(request)) { return kNotWon; }
                if (round + 1U < request.chain_rounds && request.settle != nullptr) {
                    request.settle(request.settle_us);
                }
            }
            return nullptr;
        }
        if (run_attempts(request)) { return nullptr; }
        if (step == WindowStep::W1 && request.scratch_repair_attempts != 0U) {
            /* 第一次没赢：修好 scratch 再来一轮（这次不再修）。 */
            static_cast<void>(request.repair_scratch(request.context));
            if (run_attempts(request)) { return nullptr; }
        }
        return kNotWon;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
