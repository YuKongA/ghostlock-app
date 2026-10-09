/** @file tests/perf_vote_test.cpp
 *  The voting rule, on inputs chosen so that the wrong rule answers differently.
 *
 *  Three assertions carry the weight. The tag is restored rather than cleared, which the
 *  first case shows by handing over a value that only lands inside the direct map if the
 *  nibble is set. The bounds are strict, so a value equal to either end is out. And a tie
 *  goes to the address seen first: a rule using greater-or-equal would return the last of
 *  the tied group for the same input, so the case is built to make those two answers differ.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/perf_vote.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::collect_candidates;
    using ghostlock::backend::cve_2026_43499::DirectMapBounds;
    using ghostlock::backend::cve_2026_43499::inside_direct_map;
    using ghostlock::backend::cve_2026_43499::kCandidateCapacity;
    using ghostlock::backend::cve_2026_43499::kSampleRegisterVotes;
    using ghostlock::backend::cve_2026_43499::restore_canonical;
    using ghostlock::backend::cve_2026_43499::vote_for_task;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    constexpr DirectMapBounds kMap{0xffff800000000000ULL, 0xffffc88000000000ULL};

    void the_tag_is_restored_not_cleared() {
        const std::uint64_t tagged = 0xf0ff800000100000ULL;
        const std::uint64_t restored = restore_canonical(tagged);
        expect(restored == 0xffff800000100000ULL, "the sampled value gains the canonical tag");
        expect(inside_direct_map(restored, kMap), "which puts it inside the direct map");
        /* Cleared rather than restored would give 0x0080..., which is not an address here. */
        expect(!inside_direct_map(tagged & ~(0x0fULL << 56U), kMap),
               "while clearing the nibble would leave it outside");
        expect(!inside_direct_map(kMap.begin, kMap), "the begin is out: the bounds are strict");
        expect(!inside_direct_map(kMap.end, kMap), "and so is the end");
        expect(inside_direct_map(kMap.begin + 1U, kMap) && inside_direct_map(kMap.end - 1U, kMap),
               "by one on either side is in");
    }

    void one_sample_becomes_candidates() {
        const std::uint64_t registers[4] = {0xf0ff800000100000ULL, 0x0000000000000001ULL,
                                            0xf0ff800000200000ULL, 0xf0ff800000100000ULL};
        std::uint64_t out[8] = {};
        const std::size_t kept = collect_candidates(registers, 4U, kMap, out, 8U);
        expect(kept == 3U, "the value outside the map is skipped and the rest kept in order");
        expect(out[0] == 0xffff800000100000ULL && out[1] == 0xffff800000200000ULL &&
                   out[2] == 0xffff800000100000ULL,
               "with duplicates kept, because duplicates are the vote");
        expect(kSampleRegisterVotes == 32U, "at most thirty-two registers are read");
        std::uint64_t many[64] = {};
        for (std::size_t i = 0U; i < 64U; ++i) {
            many[i] = 0xf0ff800000100000ULL;
        }
        expect(collect_candidates(many, 64U, kMap, out, 8U) == 8U, "and the output capacity stops it too");
        expect(kCandidateCapacity == 256U, "the pass holds two hundred and fifty-six");
        expect(collect_candidates(nullptr, 4U, kMap, out, 8U) == 0U, "nothing to read keeps nothing");
    }

    void the_plurality_wins_and_ties_go_first() {
        const std::uint64_t three_of_five[5] = {0x10U, 0x20U, 0x10U, 0x30U, 0x10U};
        expect(vote_for_task(three_of_five, 5U) == 0x10U, "the address seen most often wins");
        /* A tie of two against two: first seen must win, not last. */
        const std::uint64_t tie[4] = {0xaaU, 0xbbU, 0xaaU, 0xbbU};
        expect(vote_for_task(tie, 4U) == 0xaaU, "and a tie goes to the address seen first");
        const std::uint64_t reversed[4] = {0xbbU, 0xaaU, 0xbbU, 0xaaU};
        expect(vote_for_task(reversed, 4U) == 0xbbU, "which is whichever came first in that input");
        expect(vote_for_task(nullptr, 0U) == 0U && vote_for_task(three_of_five, 0U) == 0U,
               "and no candidates is zero, which callers already read as not found");
    }
}  // namespace

int main() {
    std::printf("perf vote: the tag\n");
    the_tag_is_restored_not_cleared();
    std::printf("perf vote: one sample\n");
    one_sample_becomes_candidates();
    std::printf("perf vote: the plurality\n");
    the_plurality_wins_and_ties_go_first();
    if (g_failures != 0U) {
        std::printf("perf vote: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("perf vote: PASS (tag restored, bounds strict, ties go to the first seen)\n");
    return 0;
}
