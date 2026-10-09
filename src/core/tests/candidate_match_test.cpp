/** @file tests/candidate_match_test.cpp
 *  The rule that decides a candidate, with a bucket that is not the real one.
 *
 *  The bucket function is upstream code and stays there; what belongs to this tree is
 *  the rule built on it, and a rule can be tested with any bucket that answers. The
 *  cases are agreement, one disagreement (including at the last address, where the
 *  loop has to notice), and the case the retired loop got wrong: one address compares
 *  nothing, so a match there is refused rather than reported.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/candidate_match.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::candidate_matches;
    using ghostlock::backend::cve_2026_43499::leak::canonical_candidate;
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::kTagMask;
    using ghostlock::backend::cve_2026_43499::leak::kTagSweepCount;
    using ghostlock::backend::cve_2026_43499::leak::match_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::MatchVerdict;
    using ghostlock::backend::cve_2026_43499::leak::sweep_tag;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A bucket that answers without a hash, so the rule can be watched on its own:
     *  by default every address puts a candidate in the same place, and adding a mix
     *  makes one address disagree. */
    struct FakeBuckets final {
        std::uint64_t disagreement = 0U;
        [[nodiscard]] std::uint64_t bucket(std::uint64_t address, std::uint64_t candidate) const noexcept {
            const std::uint64_t from_candidate = candidate >> 4U;
            return address == 0x2000ULL ? from_candidate + disagreement : from_candidate;
        }
    };

    void the_tag_is_the_top_nibble() {
        const std::uint64_t address = 0x1234ffff80001000ULL;
        expect(canonical_candidate(address) == 0x1f34ffff80001000ULL,
               "the canonical pass rewrites the tag and nothing else");
        for (std::uint64_t tag = 0U; tag < 16U; ++tag) {
            const std::uint64_t tagged = tagged_candidate(address, tag);
            expect((tagged & kTagMask) == (tag << 56U), "each tag lands where a tag lives");
            expect((tagged & ~kTagMask) == (address & ~kTagMask),
                   "and leaves every other bit where it was");
        }
        for (std::size_t step = 0U; step < kTagSweepCount; ++step) {
            const std::uint64_t tag = sweep_tag(step);
            expect(tag < kCanonicalTag, "the sweep never repeats the canonical tag");
            if (step > 0U) {
                expect(tag != sweep_tag(step - 1U), "and never repeats a tag it has tried");
            }
        }
        expect(kTagSweepCount == 15U, "with every remaining nibble tried once");
    }

    void agreement_is_the_whole_rule() {
        const FakeBuckets buckets;
        const std::uint64_t addresses[3] = {0x1000U, 0x2000U, 0x4000U};
        const std::uint64_t agreeing = 0x3000U;
        expect(candidate_matches(buckets, addresses, 3U, agreeing) == MatchVerdict::Matches,
               "one bucket for every address is a match");
        /* A candidate the last address disagrees about: the loop must not stop early. */
        FakeBuckets disagreeing;
        disagreeing.disagreement = 1U;
        const std::uint64_t address_pair[2] = {0x1000U, 0x2000U};
        expect(candidate_matches(disagreeing, address_pair, 2U, 0x1000U) == MatchVerdict::DoesNotMatch,
               "a disagreement at the last address is still a disagreement");
        expect(std::string_view(match_verdict_name(MatchVerdict::DoesNotMatch)) ==
                   std::string_view("no-match"),
               "and it is named");
        /* The case the retired loop answered wrongly: nothing to compare against. */
        expect(candidate_matches(buckets, addresses, 1U, agreeing) == MatchVerdict::TooFewToCompare,
               "one address compares nothing, so it cannot be a match");
        expect(candidate_matches(buckets, addresses, 0U, agreeing) == MatchVerdict::TooFewToCompare,
               "and no addresses is the same answer");
        expect(candidate_matches(buckets, nullptr, 3U, agreeing) == MatchVerdict::TooFewToCompare,
               "as is an empty list");
        expect(std::string_view(match_verdict_name(MatchVerdict::TooFewToCompare)) ==
                   std::string_view("geometry-missing"),
               "refused under the geometry name");
    }
}  // namespace

int main() {
    std::printf("candidate match: the tag\n");
    the_tag_is_the_top_nibble();
    std::printf("candidate match: the rule\n");
    agreement_is_the_whole_rule();
    if (g_failures != 0U) {
        std::printf("candidate match: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("candidate match: PASS (agreement decides, and nothing to compare is refused)\n");
    return 0;
}
