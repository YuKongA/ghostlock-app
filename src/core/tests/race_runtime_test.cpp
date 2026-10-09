/** @file tests/race_runtime_test.cpp
 *  The settings one attempt needs, and the outcomes it can report.
 *
 *  The settings are read from the profile, so the test checks the coherence rules
 *  rather than values: patience is required, and two sides on one processor is not
 *  a choice. The device's own values are used as the accepted case.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/race/race_runtime.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::race::attempt_outcome_name;
    using ghostlock::backend::cve_2026_43499::race::AttemptOutcome;
    using ghostlock::backend::cve_2026_43499::race::check_settings;
    using ghostlock::backend::cve_2026_43499::race::RaceSettings;
    using ghostlock::backend::cve_2026_43499::race::settings_verdict_name;
    using ghostlock::backend::cve_2026_43499::race::SettingsVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** What the device's document declares for the race. */
    RaceSettings device_settings() {
        RaceSettings settings;
        settings.main_cpu = 0;
        settings.consumer_cpu = 1;
        settings.settle_us = 50000U;
        settings.poll_us = 1000U;
        settings.poll_rounds = 300000U; /* route_done_timeout_ms / state_poll_interval_us */
        return settings;
    }

    void the_rules_hold() {
        expect(check_settings(device_settings()) == SettingsVerdict::Ok,
               "the settings a real device declares are coherent");
        RaceSettings no_attempts = device_settings();
        no_attempts.poll_rounds = 0U;
        expect(check_settings(no_attempts) == SettingsVerdict::NoPatience,
               "an attempt with no patience is refused");
        expect(std::string_view(settings_verdict_name(SettingsVerdict::NoPatience)) ==
                   std::string_view("tuning-missing"),
               "and named as a missing tuning value");
        RaceSettings no_interval = device_settings();
        no_interval.poll_us = 0U;
        expect(check_settings(no_interval) == SettingsVerdict::NoPatience,
               "so is one that would spin without looking");
        RaceSettings same = device_settings();
        same.consumer_cpu = same.main_cpu;
        expect(check_settings(same) == SettingsVerdict::SameProcessor,
               "two sides on one processor is not a choice");
        RaceSettings unpinned = device_settings();
        unpinned.main_cpu = -1;
        unpinned.consumer_cpu = -1;
        expect(check_settings(unpinned) == SettingsVerdict::Ok,
               "leaving both to the scheduler is allowed when the profile says so");
        expect(std::string_view(attempt_outcome_name(AttemptOutcome::HandedOver)) ==
                   std::string_view("handed-over"),
               "the outcomes keep the names the report uses");
        expect(std::string_view(attempt_outcome_name(AttemptOutcome::NeverParked)) !=
                   std::string_view("unknown"),
               "and every one of them has a name");
    }
}  // namespace

int main() {
    std::printf("race runtime: the settings\n");
    the_rules_hold();
    if (g_failures != 0U) {
        std::printf("race runtime: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("race runtime: PASS (settings checked + outcomes named)\n");
    return 0;
}
