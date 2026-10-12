/** @file tests/window_attempt_test.cpp
 *  43499「尝试一次」的证据：两个准备好的字 + 窗口守卫 + 一条判据（内核真的搬了一个等待者）。
 *
 *  假 seam 把四个 futex 调用记下来，所以这条路径在主机上可测：赢了会 disarm（解锁一次），
 *  没搬等待者就不算赢。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/steps/window_attempt.hpp"

using namespace ghostlock;

namespace {
    namespace race = backend::cve_2026_43499::race;
    namespace steps = backend::cve_2026_43499::steps;

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    /** 假 seam：记下四个调用；`moved` 决定内核"搬没搬"那个等待者。 */
    struct FakeSeam final {
        static int locks;
        static int unlocks;
        static int moved;
        static int lock_pi(std::uint32_t *) noexcept { ++locks; return 0; }
        static int cmp_requeue_pi(std::uint32_t *, std::uint32_t *) noexcept { return moved; }
        static int wait_requeue_pi(std::uint32_t *, std::uint32_t *) noexcept { return 0; }
        static int unlock_pi(std::uint32_t *) noexcept { ++unlocks; return 0; }
    };
    int FakeSeam::locks = 0;
    int FakeSeam::unlocks = 0;
    int FakeSeam::moved = 0;

    void an_attempt_that_moves_a_waiter_wins() {
        std::uint32_t chain = 0U;
        std::uint32_t target = 0U;
        race::WindowGuard window{};
        window.enter(contract::ExitPath::Success);
        steps::WindowAttempt attempt{};
        attempt.chain = &chain;
        attempt.target = &target;
        attempt.window = &window;
        FakeSeam::locks = 0;
        FakeSeam::unlocks = 0;
        FakeSeam::moved = 1;
        expect(steps::window_attempt_once<FakeSeam>(attempt), "a moved waiter is a win");
        expect(attempt.outcome == race::RaceOutcome::Running, "the outcome says running");
        expect(FakeSeam::locks == 1, "the target was locked once");
        expect(FakeSeam::unlocks == 1, "and disarmed exactly once");
    }

    void an_attempt_with_nobody_to_move_loses() {
        std::uint32_t chain = 0U;
        std::uint32_t target = 0U;
        race::WindowGuard window{};
        window.enter(contract::ExitPath::Success);
        steps::WindowAttempt attempt{};
        attempt.chain = &chain;
        attempt.target = &target;
        attempt.window = &window;
        FakeSeam::moved = 0;
        expect(!steps::window_attempt_once<FakeSeam>(attempt), "zero moved waiters is a loss");
        expect(attempt.outcome == race::RaceOutcome::Refused, "and it is named Refused");
    }

    void an_attempt_without_a_window_is_not_prepared() {
        steps::WindowAttempt attempt{};
        std::uint32_t chain = 0U;
        std::uint32_t target = 0U;
        attempt.chain = &chain;
        attempt.target = &target;
        expect(!steps::window_attempt_once<FakeSeam>(attempt), "no window is not a win");
        expect(attempt.outcome == race::RaceOutcome::NotPrepared, "and it says NotPrepared");
    }
}  // namespace

int main() {
    std::printf("window attempt: the one judgement");
    an_attempt_that_moves_a_waiter_wins();
    an_attempt_with_nobody_to_move_loses();
    an_attempt_without_a_window_is_not_prepared();
    if (g_failures != 0U) {
        std::printf("window attempt: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("window attempt: PASS (a moved waiter wins, nothing moved loses, no window refuses)\n");
    return 0;
}
