/** @file tests/leak_search_test.cpp
 *  The composition, tested where it matters: that it stops at the first address the rule
 *  accepts, that a tagged object is found when the sweep is on, and that finding nothing
 *  is an answer rather than a silence.
 *
 *  The fake bucket is built so exactly one candidate can be the answer: for that one it
 *  returns the same value for every address, and for all the others it returns something
 *  that depends on the address, which is what disagreement looks like to the rule.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/leak_search.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::search_for_object;
    using ghostlock::backend::cve_2026_43499::leak::SearchOutcome;
    using ghostlock::backend::cve_2026_43499::leak::search_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::SearchVerdict;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Agrees with itself only about the target. */
    struct OneAnswerBuckets final {
        /** Which bucket an address is in: the fake way of asking the same question the real
         *  rule answers by hashing. */
        [[nodiscard]] std::uint64_t bucket_of(std::uint64_t address) const noexcept {
            return bucket(address, address);
        }
        std::uint64_t target = 0U;
        bool has_target = false;
        [[nodiscard]] std::uint64_t bucket(std::uint64_t address, std::uint64_t candidate) const noexcept {
            if (has_target && candidate == target) {
                return 7U;
            }
            /* The two addresses these tests use are 0x1000 and 0x2000, and they have to
             *  disagree: a bucket that depends only on low bits answers the same for both,
             *  which the rule correctly reads as agreement. */
            return 3U + ((address / 0x1000U) % 2U);
        }
    };

    ScanStrides small() {
        ScanStrides strides;
        strides.coarse = 4096U;
        strides.slab = 1024U;
        strides.candidate = 256U;
        return strides;
    }

    ScanRange whole() {
        ScanRange range;
        range.start = 0U;
        range.end = 8192U;
        return range;
    }

    void it_stops_at_the_first_answer() {
        OneAnswerBuckets buckets;
        /* The third probe of the walk is address 512, canonical. */
        buckets.target = tagged_candidate(512U, kCanonicalTag);
        buckets.has_target = true;
        /* The armed addresses must be in the object's bucket, and this fake says which bucket a
         *  candidate is in by asking whether it is the target -- so being in its bucket means
         *  being it. The real rule gets that from the hash, where two addresses can share one. */
        const std::uint64_t addresses[2] = {buckets.target, buckets.target};
        const SearchOutcome found =
            search_for_object(small(), whole(), false, buckets, addresses, 2U, 1U);
        expect(found.status == SearchVerdict::Found, "the object is found");
        expect(found.found == tagged_candidate(512U, kCanonicalTag), "and it is the address the rule accepted");
        expect(found.probes == 3U, "the search stopped at the third probe, not at the end");
    }

    void a_tagged_object_needs_the_sweep() {
        OneAnswerBuckets buckets;
        buckets.target = tagged_candidate(256U, 5U);
        buckets.has_target = true;
        /* As in the case above: this fake expresses the object's bucket by being the object. */
        const std::uint64_t addresses[2] = {buckets.target, buckets.target};
        const SearchOutcome without =
            search_for_object(small(), whole(), false, buckets, addresses, 2U, 1U);
        expect(without.status == SearchVerdict::NotFound,
               "without the sweep a tagged object is not found, however long the walk is");
        expect(without.probes == 32U, "and the walk goes all the way to the end first");
        const SearchOutcome with = search_for_object(small(), whole(), true, buckets, addresses, 2U, 1U);
        expect(with.status == SearchVerdict::Found, "with it, the object is found");
        expect(with.found == tagged_candidate(256U, 5U), "under the tag it was reachable at");
        expect(with.probes == 23U,
               "twenty-three probes in: sixteen for the first address, then the second one canonical and its tags up to five");
    }

    void finding_nothing_is_an_answer() {
        const std::uint64_t addresses[2] = {0x1000U, 0x2000U};
        OneAnswerBuckets buckets;
        const SearchOutcome nothing =
            search_for_object(small(), whole(), false, buckets, addresses, 2U, 1U);
        expect(nothing.status == SearchVerdict::NotFound, "a walk with no answer says so");
        expect(nothing.probes == 32U, "after trying every address in the range");
        expect(std::string_view(search_verdict_name(SearchVerdict::NotFound)) ==
                   std::string_view("plan-missing"),
               "under the name for a plan that did not happen");
    }

    void what_cannot_be_searched() {
        const std::uint64_t addresses[2] = {0x1000U, 0x2000U};
        OneAnswerBuckets buckets;
        buckets.target = tagged_candidate(0U, kCanonicalTag);
        buckets.has_target = true;
        /* One address cannot be compared against anything: the matcher refuses, and the
         *  refusal has to travel out rather than be read as a search that found nothing. */
        expect(search_for_object(small(), whole(), false, buckets, addresses, 1U, 1U).status ==
                   SearchVerdict::GeometryMissing,
               "one address is refused rather than searched with");
        ScanStrides broken = small();
        broken.candidate = 0U;
        expect(search_for_object(broken, whole(), false, buckets, addresses, 2U, 1U).status ==
                   SearchVerdict::GeometryMissing,
               "and a stride of zero is refused before anything is walked");
        expect(std::string_view(search_verdict_name(SearchVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "both under the geometry name");
    }
}  // namespace

int main() {
    std::printf("leak search: the first answer\n");
    it_stops_at_the_first_answer();
    std::printf("leak search: a tagged object\n");
    a_tagged_object_needs_the_sweep();
    std::printf("leak search: nothing found\n");
    finding_nothing_is_an_answer();
    std::printf("leak search: what cannot be searched\n");
    what_cannot_be_searched();
    if (g_failures != 0U) {
        std::printf("leak search: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("leak search: PASS (stops at the answer, finds tagged objects, names its refusals)\n");
    return 0;
}
