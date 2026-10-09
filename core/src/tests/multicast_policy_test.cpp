/** @file tests/multicast_policy_test.cpp
 *  The handshake, against a consumer that can be told when to succeed.
 *
 *  The case worth pinning is the wait: a count read while the consumer is still inside the
 *  window reports a success it has not finished, or a failure it has not had yet. The fake
 *  below reports itself in flight for two asks after each attempt, so a handshake that
 *  skipped the wait would stop on the wrong attempt and the assertions would say so.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/route/multicast_policy.hpp"

namespace {
    using namespace ghostlock::backend::cve_2026_43499::route;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A consumer that succeeds on a chosen attempt, and pretends to be busy for a while. */
    struct FakeConsumer final {
        std::int32_t succeed_on = 0;
        std::int32_t busy_spins = 0;
        std::int32_t arm_calls = 0;
        std::int32_t disarm_calls = 0;
        std::int32_t success_count = 0;
        std::int32_t remaining_busy = 0;
        std::int32_t current_attempt = 0;

        void arm(std::int32_t attempt) noexcept {
            ++arm_calls;
            current_attempt = attempt;
            remaining_busy = busy_spins;
        }
        void disarm() noexcept { ++disarm_calls; }
        [[nodiscard]] bool inflight() noexcept {
            if (remaining_busy > 0) {
                --remaining_busy;
                return true;
            }
            return false;
        }
        [[nodiscard]] std::int32_t successes_reported() const noexcept { return success_count; }
        [[nodiscard]] std::int32_t successes() noexcept {
            if (succeed_on != 0 && current_attempt == succeed_on) {
                ++success_count;
                succeed_on = 0;
            }
            return successes_reported();
        }
        [[nodiscard]] std::int32_t calls() const noexcept { return arm_calls; }
    };

    struct FakeHold final {
        std::int64_t spins = 0;
        std::int64_t units = 0;
        void spin(std::int32_t rounds) noexcept {
            ++spins;
            units += rounds;
        }
    };

    MulticastLimits limits(std::int32_t attempts, std::int32_t hold) {
        MulticastLimits made;
        made.max_attempts = attempts;
        made.arm_hold = hold;
        return made;
    }

    void it_stops_on_the_attempt_that_worked() {
        FakeConsumer first;
        first.succeed_on = 1;
        FakeHold hold;
        const MulticastOutcome one = run_multicast_handshake(limits(128, 20), first, hold);
        expect(one.status == MulticastVerdict::Armed, "a consumer that works on the first attempt arms");
        expect(one.attempts == 1 && one.successes == 1, "and the handshake stops right there");
        FakeConsumer third;
        third.succeed_on = 3;
        FakeHold hold2;
        const MulticastOutcome three = run_multicast_handshake(limits(128, 20), third, hold2);
        expect(three.status == MulticastVerdict::Armed && three.attempts == 3,
               "and one that works on the third stops on the third");
        expect(third.arm_calls == 3 && third.disarm_calls == 3,
               "with every attempt armed and disarmed exactly once");
    }

    void it_waits_for_the_consumer_to_come_back() {
        FakeConsumer busy;
        busy.succeed_on = 1;
        busy.busy_spins = 2;
        FakeHold hold;
        const MulticastOutcome outcome = run_multicast_handshake(limits(4, 8), busy, hold);
        expect(outcome.status == MulticastVerdict::Armed, "a busy consumer is waited for, not written off");
        expect(outcome.attempts == 1, "and the wait does not count as another attempt");
        /* One hold of eight units for the attempt, plus the waits: at least two extra spins. */
        expect(hold.spins >= 3, "because the handshake asked again while it was in flight");
    }

    void it_gives_up_by_name() {
        FakeConsumer never;
        FakeHold hold;
        const MulticastOutcome outcome = run_multicast_handshake(limits(5, 4), never, hold);
        expect(outcome.status == MulticastVerdict::Exhausted, "a consumer that never works is given up on");
        expect(outcome.attempts == 5, "after exactly the attempts it was allowed");
        expect(never.arm_calls == 5, "each of which armed it");
        FakeConsumer untouched;
        FakeHold hold2;
        expect(run_multicast_handshake(limits(0, 4), untouched, hold2).status ==
                   MulticastVerdict::GeometryMissing,
               "and no attempts allowed is refused, not run");
        expect(untouched.arm_calls == 0 && hold2.spins == 0, "with nothing armed and nothing spun");
        expect(run_multicast_handshake(limits(4, 0), untouched, hold2).status ==
                   MulticastVerdict::GeometryMissing,
               "nor is a hold of no length a handshake");
    }
}  // namespace

int main() {
    std::printf("multicast policy: the attempt that worked\n");
    it_stops_on_the_attempt_that_worked();
    std::printf("multicast policy: waiting for the consumer\n");
    it_waits_for_the_consumer_to_come_back();
    std::printf("multicast policy: giving up\n");
    it_gives_up_by_name();
    if (g_failures != 0U) {
        std::printf("multicast policy: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("multicast policy: PASS (arms, waits, stops on the attempt that worked)\n");
    return 0;
}
