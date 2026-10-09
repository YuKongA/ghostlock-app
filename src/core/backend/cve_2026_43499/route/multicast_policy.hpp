#pragma once
/** @file backend/cve_2026_43499/route/multicast_policy.hpp
 *  The multicast handshake, as a decision: arm, hold, wait, look.
 *
 *  The retired route ran a loop: arm the consumer, spin for a while so it has time inside
 *  the window, disarm, wait for it to come back, and look at whether it succeeded -- then
 *  do it again, up to a limit, until it does. Every part of that is a decision about when to
 *  stop waiting and what to report, and the consumer itself is a thread the route does not
 *  own. So the consumer is an interface here rather than a global: the handshake can be run
 *  against a fake that succeeds on the third attempt, which is the only way to test it.
 *
 *  The two numbers the loop runs on -- the attempts and how long a hold lasts -- were
 *  constexpr in the retired file. This tree keeps them out of the code: a retry count and a
 *  spin length are exactly the values that differ between devices, so they arrive as
 *  arguments and where they come from is the caller business.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::route {
    /** What the handshake needs of the consumer: arm it, let it run, and look at it. */
    template <class Consumer>
    concept MulticastConsumer = requires(Consumer &consumer, std::int32_t attempt) {
        { consumer.arm(attempt) } -> std::same_as<void>;
        { consumer.disarm() } -> std::same_as<void>;
        { consumer.inflight() } -> std::same_as<bool>;
        { consumer.successes() } -> std::same_as<std::int32_t>;
        { consumer.calls() } -> std::same_as<std::int32_t>;
    };

    /** How long to hold: whatever the caller uses to give the consumer its window. */
    template <class Hold>
    concept MulticastHold = requires(Hold &hold, std::int32_t rounds) {
        { hold.spin(rounds) } -> std::same_as<void>;
    };

    /** What the handshake was told to do. */
    struct MulticastLimits final {
        /** How many attempts before giving up. */
        std::int32_t max_attempts = 0;
        /** How long each hold lasts, in whatever unit the hold counts in. */
        std::int32_t arm_hold = 0;
    };

    /** How the handshake ended. */
    enum class MulticastVerdict : unsigned char {
        /** The consumer succeeded inside the window. */
        Armed = 0,
        /** Every attempt was spent without a success. */
        Exhausted = 1,
        /** The limits cannot be used, so nothing was tried. */
        GeometryMissing = 2,
    };

    [[nodiscard]] constexpr const char *multicast_verdict_name(MulticastVerdict verdict) noexcept {
        switch (verdict) {
            case MulticastVerdict::Armed: return "ok";
            case MulticastVerdict::Exhausted: return "plan-missing";
            case MulticastVerdict::GeometryMissing: return "geometry-missing";
        }
        return "unknown";
    }

    /** What the handshake did. */
    struct MulticastOutcome final {
        MulticastVerdict status = MulticastVerdict::GeometryMissing;
        /** Which attempt it was on when it stopped, one-based. */
        std::int32_t attempts = 0;
        /** What the consumer reported at the end. */
        std::int32_t successes = 0;
        std::int32_t calls = 0;
    };

    /** Run the handshake. Each attempt arms, holds, disarms and then waits for the consumer
     *  to come back before looking at it: reading the count while it is still inside the
     *  window would report a success it has not finished, and a failure it has not had. */
    template <MulticastConsumer Consumer, MulticastHold Hold>
    [[nodiscard]] MulticastOutcome run_multicast_handshake(const MulticastLimits &limits,
                                                           Consumer &consumer, Hold &hold) noexcept {
        MulticastOutcome outcome{};
        if (limits.max_attempts <= 0 || limits.arm_hold <= 0) {
            return outcome;
        }
        for (std::int32_t attempt = 1; attempt <= limits.max_attempts; ++attempt) {
            outcome.attempts = attempt;
            consumer.arm(attempt);
            hold.spin(limits.arm_hold);
            consumer.disarm();
            while (consumer.inflight()) {
                /* Wait for the consumer to come back rather than reading a count it may
                 *  still be writing. The hold is what keeps this from being a busy ask. */
                hold.spin(1);
            }
            if (consumer.successes() > 0) {
                break;
            }
        }
        outcome.successes = consumer.successes();
        outcome.calls = consumer.calls();
        outcome.status = outcome.successes > 0 ? MulticastVerdict::Armed : MulticastVerdict::Exhausted;
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::route
