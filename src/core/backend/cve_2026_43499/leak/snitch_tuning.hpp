#pragma once
/** @file backend/cve_2026_43499/leak/snitch_tuning.hpp
 *  The numbers the collision search is tuned with, and what happens when one is missing.
 *
 *  How many collisions to look for, how many times to wake the cohort, how long each round
 *  waits and how many threads search: these vary with a device, so they are the profile's
 *  and not this code's. The retired tree kept defaults for them in C++, and those defaults
 *  are what this tree removed -- a search that quietly runs with a number nobody chose is a
 *  search whose results cannot be explained afterwards.
 *
 *  So this reader asks the profile for each of them and refuses by name when one is not
 *  there. The key names are arguments rather than constants here, because landing a key
 *  means touching the schema, the manifest and the app in one batch, and that is a decision
 *  this file should not be able to make by itself.
 *
 *  The values themselves are read through the same neutral key reader the rest of the tree
 *  uses, so a key that is absent, a key that appears twice and a key whose value cannot be
 *  used are all reported the same way they are reported everywhere else.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_scan.hpp"
#include "memory/target_profile.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    using ghostlock::memory::KeyView;
    using ghostlock::memory::read_key;

    /** The keys the search reads. Names are the caller's, and land with the schema. */
    struct SnitchTuningKeys final {
        std::string_view collisions{};
        std::string_view wake_rounds{};
        std::string_view round_timeout_us{};
        std::string_view thread_count{};
    };

    /** What the profile said. */
    struct SnitchTuning final {
        std::uint64_t collisions = 0U;
        std::uint64_t thread_count = 0U;
        std::uint32_t wake_rounds = 0U;
        std::int64_t round_timeout_us = 0;
    };

    /** Why the search cannot be tuned. `tuning-missing` is the tree's name for a profile
     *  value that should have been there and was not. */
    enum class TuningVerdict : unsigned char { Ok = 0, TuningMissing = 1 };

    [[nodiscard]] constexpr const char *tuning_verdict_name(TuningVerdict verdict) noexcept {
        return verdict == TuningVerdict::Ok ? "ok" : "tuning-missing";
    }

    /** Read the four numbers. Nothing is handed over on a refusal, and every refusal is the
     *  same one: a search that is missing any of its numbers is not one to run. */
    [[nodiscard]] constexpr TuningVerdict resolve_snitch_tuning(const KeyView &view,
                                                                const SnitchTuningKeys &keys,
                                                                SnitchTuning &out) noexcept {
        out = SnitchTuning{};
        const auto collisions = read_key(view, keys.collisions);
        const auto wake_rounds = read_key(view, keys.wake_rounds);
        const auto timeout = read_key(view, keys.round_timeout_us);
        const auto threads = read_key(view, keys.thread_count);
        if (!collisions.ok || !wake_rounds.ok || !timeout.ok || !threads.ok) {
            return TuningVerdict::TuningMissing;
        }
        /* A cohort that cannot compare and a search with nothing to run in are not tunings
         *  either, whatever the profile says: refuse them here rather than let the search
         *  find out. */
        if (collisions.value < 2U || threads.value == 0U || wake_rounds.value == 0U ||
            wake_rounds.value > 0xffffffffULL || timeout.value == 0U) {
            return TuningVerdict::TuningMissing;
        }
        out.collisions = collisions.value;
        out.thread_count = threads.value;
        out.wake_rounds = static_cast<std::uint32_t>(wake_rounds.value);
        out.round_timeout_us = static_cast<std::int64_t>(timeout.value);
        return TuningVerdict::Ok;
    }
    /** Hand a resolved tuning to the search: the counts it derives from and the patience it
     *  waits with. The two structures the protocol takes are filled here so that nothing
     *  between the profile and the protocol has to know what a key is called. */
    constexpr void apply_snitch_tuning(const SnitchTuning &tuning, SnitchSetup &setup,
                                       SnitchPatience &patience) noexcept {
        setup.collisions = tuning.collisions;
        setup.thread_count = tuning.thread_count;
        patience.wake_rounds = tuning.wake_rounds;
        patience.round_timeout_us = tuning.round_timeout_us;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
