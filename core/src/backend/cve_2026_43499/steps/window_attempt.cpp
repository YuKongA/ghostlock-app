/** @file backend/cve_2026_43499/steps/window_attempt.cpp
 *  The device attempt: **一次尝试就是让引擎跑一次**。
 *
 *  判据的唯一权威在引擎里（\`MulticastRoute::execute\` 自带三个角色与改挂，其 \`MulticastRouteReport.status\`
 *  就是"赢没赢"）。本层此前手搓过一套三角色编排，已按"一处权威"撤掉：两套并存只会互相解释不清。
 *
 *  引擎由**调用方持有**（与 \`WordsRegion\` 同一套归属规则），本层只借指针。
 *  记录：docs/analysis/43499-route-execution-batch-plan.md 步骤 3。
 */
#include "backend/cve_2026_43499/steps/window_attempt.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    bool window_attempt_once_device(void *context) noexcept {
        if (context == nullptr) { return false; }
        auto *attempt = static_cast<WindowAttempt *>(context);
        if (attempt->route == nullptr) {
            /* 没有引擎就不算"跑过一次尝试"：说**没准备**，不说"输了"——名字要指得准。 */
            attempt->outcome = race::RaceOutcome::NotPrepared;
            return false;
        }
        const bool ok = attempt->route->execute(attempt->payload_target, attempt->payload_value, 0U);
        attempt->outcome = ok ? race::RaceOutcome::Running : race::RaceOutcome::Refused;
        return ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
