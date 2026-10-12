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
#include "support/log.hpp"   /* 出 PI 的打印（2026-10-12 裁定：PI 内只落字段，PI 之后才打印 ✓） */
/* 设备侧的具体路由类型：本单元为它显式实例化门面模板（见文件末尾）。 */
#include "backend/cve_2026_43499/seam/multicast_route.hpp"
#include "backend/cve_2026_43499/seam/multicast_route_linux.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    template <seam::RouteExec Route>
    bool window_attempt_once_device(void *context) noexcept {
        if (context == nullptr) { return false; }
        auto *attempt = static_cast<WindowAttempt<Route> *>(context);
        /* 旧码（原文保留 ✓）：
         *     if (attempt->route == nullptr) { attempt->outcome = race::RaceOutcome::NotPrepared; return false; }
         *     const bool ok = attempt->route->execute(attempt->payload_target, attempt->payload_value, 0U);
         *  TODO(access-split): 2026-10-12 裁定「旧实现接入 kernel access」—— 直调 `execute` ✗
         *  **绕过 warm-up** ✗（`arm_sequence` 注释逐字：「a run without them is not the same run」）
         *  ⇒ 有门面时经它落字（`MulticastWindow` 里先 `arm` 再 `execute` ✓）；
         *  `route` 留作**具名回退**：两者都没有时仍说 `NotPrepared`，绝不静默直写 ✗。
         *  完成 = 门面接线 ＋ 门禁全绿 ＋ 真机判据 ＋ 用户确认 ⇒ 删本注释块与回退。 */
        const bool by_seam = attempt->seam_landing != nullptr;
        /* ★ C 段判据 ✓（接入是否生效 ✓）：本次落字走**门面**还是**回退** ✗ —— 本处**在 PI 之外**
         *  （PI 由引擎里的线程持有）⇒ 按 2026-10-12 裁定**直接打印**，不进环；行名与取值与撤环前
         *  逐字一致（attempt-door a=1 门面 / a=0 具名回退 ✓）。 */
        if (!by_seam && attempt->route == nullptr) {
            /* 没有引擎就不算"跑过一次尝试"：说**没准备**，不说"输了"——名字要指得准。 */
            attempt->outcome = race::RaceOutcome::NotPrepared;
            return false;
        }
        const bool ok = by_seam
                            ? attempt->seam_landing->land_word(attempt->payload_target,
                                                               attempt->payload_value, 0U)
                            : attempt->route->execute(attempt->payload_target,
                                                      attempt->payload_value, 0U);
        attempt->outcome = ok ? race::RaceOutcome::Running : race::RaceOutcome::Refused;
        ghostlock::support::log("info", "attempt-door", by_seam ? 1 : 0, 0);
        return ok;
    }

    /* 设备只造一种路由类型：在这里为它显式实例化，定义因此可以留在本单元（与 runner 同一做法）。
     *  这也是一处编译期判据：路由不再满足窗口的 concept 时，本行先编译不过。 */
    template bool window_attempt_once_device<seam::MulticastRoute<seam::LinuxRouteOps>>(
            void *context) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
