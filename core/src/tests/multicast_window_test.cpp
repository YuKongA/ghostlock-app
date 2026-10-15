/** @file tests/multicast_window_test.cpp
 *  The window body: the warm-up, then the write, with a controller that records.
 *
 *  What is pinned is the behaviour the old handshake had and the new one lost: the opening rounds
 *  set the socket option without arming, and a run that arms every round is not the same run. It
 *  also pins the refusal: a warm-up count the caller never gave is refused by name rather than
 *  treated as zero, because a tuning number living in the code is what the window rules forbid.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/landing/multicast_window.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::MulticastLimits;
    using ghostlock::backend::cve_2026_43499::landing::MulticastWindow;
    using ghostlock::backend::cve_2026_43499::landing::WindowOutcome;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The recording route: the window's controller. The shape is the window's concept, so no base. */
    class RecordingExec final {
    public:
        int arms = 0;
        int executes = 0;
        std::uint64_t address = 0U;
        std::uint64_t value = 0U;
        std::uint64_t offset = 0U;

        [[nodiscard]] bool execute(std::uint64_t a, std::uint64_t v, std::uint64_t o) noexcept {
            ++executes;
            address = a;
            value = v;
            offset = o;
            return true;
        }
        [[nodiscard]] bool arm(std::int32_t) noexcept {
            ++arms;
            return true;
        }
    };

    void a_warm_up_count_is_required() {
        RecordingExec exec;
        const MulticastLimits unsaid;
        MulticastWindow<RecordingExec> window(exec, unsaid);
        expect(!window.write_word(0x1000U, 1U, 0U), "a caller who never gave a warm-up count is refused");
        expect(window.outcome() == WindowOutcome::NoWarmUp, "and told so by name");
        expect(exec.arms == 0 && exec.executes == 0, "without touching the controller at all");
    }

    void the_warm_up_runs_before_the_write() {
        RecordingExec exec;
        MulticastLimits limits;
        limits.max_attempts = 4;
        limits.arm_sequence = 3;
        MulticastWindow<RecordingExec> window(exec, limits);
        expect(window.warm_up_rounds() == 3, "the count is the caller's, not a code default");
        expect(window.write_word(0x2000U, 0x4142434445464748ULL, 0x40U), "the write lands");
        expect(exec.arms == 3, "after exactly three opening rounds");
        expect(exec.executes == 1 && exec.address == 0x2000U && exec.value == 0x4142434445464748ULL,
               "and one execution carrying the address and value through");
        expect(exec.offset == 0x40U, "with the offset the parameters gave");
        expect(window.outcome() == WindowOutcome::Wrote, "and the outcome says so");
    }
}  // namespace

int main() {
    std::printf("multicast window: the warm-up the old handshake had\n");
    a_warm_up_count_is_required();
    the_warm_up_runs_before_the_write();
    if (g_failures != 0U) {
        std::printf("multicast window: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("multicast window: PASS (warm-up rounds, then one armed write)\n");
    return 0;
}
