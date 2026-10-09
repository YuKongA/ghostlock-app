#pragma once
/** @file race/race_runtime.hpp
 *  What one race attempt needs from a profile, and what it can conclude.
 *
 *  Which processor each side runs on, how long the kernel is given to settle
 *  after a waiter parks, how often the state is looked at and how many times
 *  before giving up: every one of those is a setting, and every one of them comes
 *  from the profile as a resolved pair. This file holds the settings, checks them
 *  for coherence, and names the outcomes; it starts no threads and issues no
 *  syscall, because the caller owns the two threads and the layer owns only the
 *  shape (a thread allocates, and the window forbids that).
 */
#include <cstddef>
#include <cstdint>

#include "memory/target_profile.hpp"

namespace ghostlock::backend::cve_2026_43499::race {
    /** What the profile says about one attempt. */
    struct RaceSettings final {
        /** The processors the profile names for the two sides, or -1 to leave
         *  them to the scheduler. */
        int main_cpu = -1;
        int consumer_cpu = -1;
        /** How long the kernel is given to settle after the waiter parks. */
        std::uint64_t settle_us = 0U;
        /** How often the state is looked at while waiting. */
        std::uint64_t poll_us = 0U;
        /** How many rounds of looking the profile's own waiting time allows. This
         *  is not a setting of its own: the document declares a timeout and an
         *  interval, and the rounds follow from them. */
        std::uint64_t poll_rounds = 0U;
    };

    /** Why the settings cannot be used. Names follow the single table. */
    enum class SettingsVerdict : unsigned char {
        Ok = 0,
        /** No patience declared: an attempt that cannot wait cannot conclude. */
        NoPatience = 1,
        /** Two sides on one processor defeats the point of choosing. */
        SameProcessor = 2,
    };

    [[nodiscard]] constexpr const char *settings_verdict_name(SettingsVerdict verdict) noexcept {
        switch (verdict) {
            case SettingsVerdict::Ok: return "ok";
            case SettingsVerdict::NoPatience: return "tuning-missing";
            case SettingsVerdict::SameProcessor: return "param-invalid";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr SettingsVerdict check_settings(const RaceSettings &settings) noexcept {
        if (settings.poll_rounds == 0U || settings.poll_us == 0U) {
            return SettingsVerdict::NoPatience;
        }
        if (settings.main_cpu >= 0 && settings.main_cpu == settings.consumer_cpu) {
            return SettingsVerdict::SameProcessor;
        }
        return SettingsVerdict::Ok;
    }

    /** What one attempt did. */
    enum class AttemptOutcome : unsigned char {
        /** The waiter parked and the kernel handed it the lock. */
        HandedOver = 0,
        /** The waiter never parked within the settings' patience. */
        NeverParked = 1,
        /** A call was refused: a seam or kernel problem, not a race outcome. */
        Refused = 2,
    };

    [[nodiscard]] constexpr const char *attempt_outcome_name(AttemptOutcome outcome) noexcept {
        switch (outcome) {
            case AttemptOutcome::HandedOver: return "handed-over";
            case AttemptOutcome::NeverParked: return "never-parked";
            case AttemptOutcome::Refused: return "refused";
        }
        return "unknown";
    }
}  // namespace ghostlock::backend::cve_2026_43499::race
