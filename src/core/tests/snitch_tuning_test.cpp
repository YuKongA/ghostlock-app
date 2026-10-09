/** @file tests/snitch_tuning_test.cpp
 *  The search's numbers come from the profile, or they do not come at all.
 *
 *  Two things are pinned here. One is that a missing key refuses the whole tuning rather
 *  than leaving a default behind: the retired tree had defaults in C++, and a search that
 *  runs with a number nobody chose cannot be explained afterwards. The other is that a value
 *  which cannot be used is refused the same way as one that is absent -- a cohort asked for
 *  one collision, a round count of zero, a timeout of zero -- because the profile saying
 *  something is not the same as the profile saying something usable.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_tuning.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::resolve_snitch_tuning;
    using ghostlock::backend::cve_2026_43499::leak::SnitchTuning;
    using ghostlock::backend::cve_2026_43499::leak::SnitchTuningKeys;
    using ghostlock::backend::cve_2026_43499::leak::tuning_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::TuningVerdict;
    using ghostlock::memory::KeyValue;
    using ghostlock::memory::KeyView;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    constexpr SnitchTuningKeys kKeys{"heap.collisions", "heap.wake_rounds", "heap.round_timeout_us",
                                     "heap.thread_count"};

    KeyView view_of(const KeyValue *items, std::size_t count) {
        KeyView view;
        view.items = items;
        view.count = count;
        return view;
    }

    KeyView complete() {
        static const KeyValue all[] = {{kKeys.collisions, 4U},
                                       {kKeys.wake_rounds, 200U},
                                       {kKeys.round_timeout_us, 2000U},
                                       {kKeys.thread_count, 2U}};
        return view_of(all, 4U);
    }

    void a_complete_profile_tunes_the_search() {
        SnitchTuning tuning;
        expect(resolve_snitch_tuning(complete(), kKeys, tuning) == TuningVerdict::Ok,
               "four keys in the profile tune the search");
        expect(tuning.collisions == 4U && tuning.thread_count == 2U, "with the counts it was given");
        expect(tuning.wake_rounds == 200U && tuning.round_timeout_us == 2000, "and its patience");
    }

    void a_missing_key_refuses_the_whole_thing() {
        SnitchTuning tuning;
        static const KeyValue three[] = {{kKeys.collisions, 4U}, {kKeys.wake_rounds, 200U},
                                        {kKeys.thread_count, 2U}};
        expect(resolve_snitch_tuning(view_of(three, 3U), kKeys, tuning) == TuningVerdict::TuningMissing,
               "a profile missing the round timeout does not tune anything");
        expect(tuning.collisions == 0U && tuning.wake_rounds == 0U,
               "and nothing half-filled is handed over");
        expect(resolve_snitch_tuning(view_of(nullptr, 0U), kKeys, tuning) == TuningVerdict::TuningMissing,
               "nor does an empty profile");
        expect(std::string_view(tuning_verdict_name(TuningVerdict::TuningMissing)) ==
                   std::string_view("tuning-missing"),
               "refused under the name the tree uses for a profile value that should be there");
    }

    void a_value_that_cannot_be_used_is_the_same_refusal() {
        SnitchTuning tuning;
        static const KeyValue one_collision[] = {{kKeys.collisions, 1U},
                                                 {kKeys.wake_rounds, 200U},
                                                 {kKeys.round_timeout_us, 2000U},
                                                 {kKeys.thread_count, 2U}};
        expect(resolve_snitch_tuning(view_of(one_collision, 4U), kKeys, tuning) ==
                   TuningVerdict::TuningMissing,
               "one collision compares nothing, so it is not a tuning");
        static const KeyValue no_rounds[] = {{kKeys.collisions, 4U},
                                             {kKeys.wake_rounds, 0U},
                                             {kKeys.round_timeout_us, 2000U},
                                             {kKeys.thread_count, 2U}};
        expect(resolve_snitch_tuning(view_of(no_rounds, 4U), kKeys, tuning) == TuningVerdict::TuningMissing,
               "nor is patience of no rounds");
        static const KeyValue no_wait[] = {{kKeys.collisions, 4U},
                                           {kKeys.wake_rounds, 200U},
                                           {kKeys.round_timeout_us, 0U},
                                           {kKeys.thread_count, 2U}};
        expect(resolve_snitch_tuning(view_of(no_wait, 4U), kKeys, tuning) == TuningVerdict::TuningMissing,
               "nor a round that waits for nothing, which a device run already showed to be the difference between finding out and giving up");
        static const KeyValue no_threads[] = {{kKeys.collisions, 4U},
                                              {kKeys.wake_rounds, 200U},
                                              {kKeys.round_timeout_us, 2000U},
                                              {kKeys.thread_count, 0U}};
        expect(resolve_snitch_tuning(view_of(no_threads, 4U), kKeys, tuning) == TuningVerdict::TuningMissing,
               "nor a search with no thread to run in");
    }
}  // namespace

int main() {
    std::printf("snitch tuning: a complete profile\n");
    a_complete_profile_tunes_the_search();
    std::printf("snitch tuning: a missing key\n");
    a_missing_key_refuses_the_whole_thing();
    std::printf("snitch tuning: values that cannot be used\n");
    a_value_that_cannot_be_used_is_the_same_refusal();
    if (g_failures != 0U) {
        std::printf("snitch tuning: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch tuning: PASS (from the profile, or refused by name)\n");
    return 0;
}
