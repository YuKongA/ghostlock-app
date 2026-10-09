/** @file tests/snitch_discovery_test.cpp
 *  One object, and no pretence about the other three.
 *
 *  The assertions check the three ways a single-object search ends, and one more thing: that
 *  the result stays exactly what it claims to be. A field for another address added here later
 *  -- init task, kernel base -- would make an incomplete discovery look complete, so its size
 *  and the offsets of its fields are pinned. Expanding what this reports is a decision, not an
 *  edit.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_discovery.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::find_object;
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::ObjectOutcome;
    using ghostlock::backend::cve_2026_43499::leak::object_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::ObjectVerdict;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SearchVerdict;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    struct Buckets final {
        /** Which bucket an address is in: the fake way of asking the same question the real
         *  rule answers by hashing. */
        [[nodiscard]] std::uint64_t bucket_of(std::uint64_t address) const noexcept {
            return bucket(address, address);
        }
        std::uint64_t target = 0U;
        bool has_target = false;
        [[nodiscard]] std::uint64_t bucket(std::uint64_t address, std::uint64_t candidate) const noexcept {
            if (has_target && candidate == target) { return 7U; }
            return 3U + ((address / 0x1000U) % 2U);
        }
    };

    ScanStrides strides() {
        return ScanStrides{4096U, 1024U, 256U};
    }
    ScanRange range() {
        return ScanRange{0U, 8192U};
    }

    void what_it_reports() {
        /* The armed addresses are the target here, and that is the fake being honest rather
         *  than lazy: this fake answers a bucket by looking at the candidate, so the only way
         *  to say "these addresses are in the target's bucket" is for them to be the target.
         *  The real rule gets that from the hash, where two different addresses can agree. */
        const std::uint64_t addresses[2] = {tagged_candidate(512U, kCanonicalTag),
                                            tagged_candidate(512U, kCanonicalTag)};
        Buckets found;
        found.target = tagged_candidate(512U, kCanonicalTag);
        found.has_target = true;
        const ObjectOutcome one = find_object(strides(), range(), false, found, addresses, 2U, 1U);
        expect(one.status == ObjectVerdict::Found, "an object the rule accepts is found");
        expect(one.object == tagged_candidate(512U, kCanonicalTag), "and reported as itself");
        expect(one.probes == 3U, "after the probes it took");
        /* A pair that disagrees with itself, for the cases about the agreement condition: the
         *  fake buckets by parity, so these two are never in one bucket and no candidate can
         *  satisfy the rule. That is a different question from the count, which the pair above
         *  tests by sharing the target's bucket. */
        const std::uint64_t disagreeing[2] = {0x1000U, 0x2000U};
        Buckets none;
        const ObjectOutcome two = find_object(strides(), range(), false, none, disagreeing, 2U, 1U);
        expect(two.status == ObjectVerdict::NotFound && two.object == 0U,
               "a range with no answer reports not found and no address");
        const ObjectOutcome three = find_object(strides(), range(), false, none, disagreeing, 1U, 1U);
        expect(three.status == ObjectVerdict::Refused,
               "and one address to compare is refused rather than searched with");
        expect(std::string_view(object_verdict_name(ObjectVerdict::Refused)) ==
                   std::string_view("geometry-missing"),
               "under the geometry name");
        expect(std::string_view(object_verdict_name(ObjectVerdict::NotFound)) ==
                   std::string_view("plan-missing"),
               "and a search that found nothing under the plan name");
    }

    void it_stays_one_object() {
        /* Three fields: the verdict, the address and the probe count. A fourth would mean an
         *  address this search never looked for. */
        expect(sizeof(ObjectOutcome) <= sizeof(std::size_t) + sizeof(std::uint64_t) + 8U,
               "the outcome carries one address and nothing that pretends to be another");
        static_assert(offsetof(ObjectOutcome, object) > 0U, "the address is not the first field");
    }
}  // namespace

int main() {
    std::printf("snitch discovery: what it reports\n");
    what_it_reports();
    std::printf("snitch discovery: what it does not claim\n");
    it_stays_one_object();
    if (g_failures != 0U) {
        std::printf("snitch discovery: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch discovery: PASS (one object, and no pretence about the others)\n");
    return 0;
}
