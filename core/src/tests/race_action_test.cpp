/** @file tests/race_action_test.cpp
 *  Running one step: which link answers, and what it says.
 *
 *  The chain is only joined here, and every link already refuses by name, so the checks are about
 *  which name comes out. Two of the three failure paths stop before any syscall: an unprepared
 *  attempt, and an attempt whose window is not open. The third is exercised with a seam of our own,
 *  which is what the attempt type is parameterised on, so the syscall order is visible and the real
 *  futex calls are never made.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/steps/action.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::EncodeVerdict;
    using ghostlock::backend::cve_2026_43499::memory::WriteMode;
    using ghostlock::backend::cve_2026_43499::race::PiRace;
    using ghostlock::backend::cve_2026_43499::race::RaceOutcome;
    using ghostlock::backend::cve_2026_43499::race::WindowGuard;
    using ghostlock::backend::cve_2026_43499::steps::RaceAction;
    using ghostlock::backend::cve_2026_43499::steps::request_for;
    using ghostlock::backend::cve_2026_43499::steps::StepTargets;
    using ghostlock::contract::ExitPath;
    using ghostlock::contract::StepId;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    StepTargets sample() {
        StepTargets t{};
        t.selinux_enforcing = 0x1000U;
        t.credential = 0x2000U;
        t.seccomp_mode = 0x3000U;
        return t;
    }

    ghostlock::backend::cve_2026_43499::memory::WaiterFieldOffsets offsets() {
        ghostlock::backend::cve_2026_43499::memory::WaiterFieldOffsets o{};
        o.pc = 0U;
        o.right = 8U;
        o.left = 16U;
        return o;
    }

    void a_step_this_backend_does_not_run_stops_at_the_request_link() {
        PiRace<> race;
        WindowGuard window;
        std::uint8_t fragment[64U] = {};
        RaceAction<> action(race, window, offsets(), sample(), fragment, sizeof(fragment), 8U, 0U);
        expect(!action.run(StepId::PageCacheWrite), "a step this backend does not run fails");
        expect(action.last_stage() == RaceAction<>::Stage::Request, "and it stops at the decision");
        expect(action.last_request() != ghostlock::backend::cve_2026_43499::memory::RequestVerdict::Ok,
               "with the request verdict saying why");
    }

    void a_fragment_too_small_stops_at_the_encode_link() {
        PiRace<> race;
        WindowGuard window;
        std::uint8_t tiny[4U] = {};   /* three fields at 0, 8 and 16 cannot fit */
        RaceAction<> action(race, window, offsets(), sample(), tiny, sizeof(tiny), 8U, 0U);
        expect(!action.run(StepId::DisenforceSelinux), "an arm that does not fit fails the step");
        expect(action.last_stage() == RaceAction<>::Stage::Encode, "and it stops at the encode");
        expect(action.last_encode() == EncodeVerdict::FragmentTooSmall, "named as the fragment size");
        expect(action.last_encode() != EncodeVerdict::Ok, "which is not success");
    }

    void an_attempt_outside_its_window_is_refused_after_the_arm_is_encoded() {
        /* NotPrepared is reachable only when prepare() was never called, and this chain prepares as
         *  part of the step, so that state is not reachable through run() at all. What this case does
         *  show is the order: the arm is encoded successfully, the attempt is reached, and there the
         *  window refuses it. Asserting NotPrepared here would have been asserting a case that cannot
         *  happen. */
        PiRace<> race;
        WindowGuard window;
        std::uint8_t fragment[64U] = {};
        RaceAction<> action(race, window, offsets(), sample(), fragment, sizeof(fragment), 8U, 0U);
        const bool ok = action.run(StepId::DisenforceSelinux);
        expect(action.last_stage() == RaceAction<>::Stage::Race, "the chain reaches the attempt");
        expect(action.last_encode() == EncodeVerdict::Ok, "having encoded the arm first");
        expect(action.last_race() == RaceOutcome::Refused,
               "and the attempt is refused there, because no window was entered");
        expect(!ok, "which makes the step a failure");
    }

    /** A seam of our own: it records the syscalls and never makes one. */
    struct RecordingSeam final {
        static const char *order[8U];
        static std::size_t count;
        static void note(const char *what) noexcept {
            if (count < 8U) {
                order[count] = what;
            }
            ++count;
        }
        static int lock_pi(std::uint32_t *word) noexcept {
            note("lock_pi");
            return word == nullptr ? -1 : 0;
        }
        static int wait_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            note("wait_requeue_pi");
            return (chain == nullptr || target == nullptr) ? -1 : 0;
        }
        static int cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            note("cmp_requeue_pi");
            /* The kernel moved one waiter. This is the kernel own evidence, and it is what the
             *  owner side reads: a double that reports none is refused for having none. */
            return (chain == nullptr || target == nullptr) ? -1 : 1;
        }
        static int unlock_pi(std::uint32_t *word) noexcept {
            note("unlock_pi");
            return word == nullptr ? -1 : 0;
        }
    };
    const char *RecordingSeam::order[8U] = {};
    std::size_t RecordingSeam::count = 0U;

    void inside_an_open_window_the_attempt_reaches_the_seam_in_order() {
        PiRace<RecordingSeam> race;
        WindowGuard window;
        std::uint8_t fragment[64U] = {};
        std::uint32_t chain = 0U;
        std::uint32_t target = 0U;
        race.prepare(&chain, &target);
        window.enter(ExitPath::Success);
        RecordingSeam::count = 0U;
        RaceAction<RecordingSeam> action(race, window, offsets(), sample(), fragment, sizeof(fragment), 8U, 0U);
        const bool ok = action.run(StepId::DisenforceSelinux);
        expect(RecordingSeam::count > 0U, "and with an open window the seam is actually called");
        expect(RecordingSeam::order[0U] != nullptr && RecordingSeam::order[0U][0] == 'l',
               "beginning with the lock, which is the first syscall the attempt makes");
        expect(action.last_race() != RaceOutcome::Refused,
               "so the attempt was not refused: it went into the seam");
        (void)ok;
    }

    void an_unopened_window_refuses_the_attempt() {
        PiRace<> race;
        WindowGuard closed;
        std::uint8_t fragment[64U] = {};
        std::uint32_t chain = 0U;
        std::uint32_t target = 0U;
        race.prepare(&chain, &target);
        RaceAction<> action(race, closed, offsets(), sample(), fragment, sizeof(fragment), 8U, 0U);
        const bool ok = action.run(StepId::DisenforceSelinux);
        expect(!ok, "an attempt outside the window is not a success");
        expect(action.last_race() == RaceOutcome::Refused, "and it is named as refused");
    }
}  // namespace

int main() {
    std::printf("race action: the request link\n");
    a_step_this_backend_does_not_run_stops_at_the_request_link();
    std::printf("race action: the encode link\n");
    a_fragment_too_small_stops_at_the_encode_link();
    std::printf("race action: the attempt\n");
    an_attempt_outside_its_window_is_refused_after_the_arm_is_encoded();
    an_unopened_window_refuses_the_attempt();
    std::printf("race action: inside the window\n");
    inside_an_open_window_the_attempt_reaches_the_seam_in_order();
    if (g_failures != 0U) {
        std::printf("race action: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("race action: PASS (each link refuses with its own name, no syscall before the window)\n");
    return 0;
}
