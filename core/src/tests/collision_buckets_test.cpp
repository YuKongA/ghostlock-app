/** @file tests/collision_buckets_test.cpp
 *  The collision rule against the kernel's own hash, and a statement of what it can decide.
 *
 *  The addresses are not invented: the test walks candidates until it finds two whose buckets are
 *  equal and a third whose bucket differs, and asserts the rule reports exactly that. It also
 *  asserts the limit the rule has -- the agreement check passes for any candidate, because the
 *  answer is the candidate's own bucket -- so that nobody later reads the check as a filter it is
 *  not. What separates the object from everything else is that the proof addresses were observed
 *  to collide and that the walk's range decides which candidates are ever asked about.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/candidate_match.hpp"
#include "backend/cve_2026_43499/leak/collision_buckets.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::candidate_matches;
    using ghostlock::backend::cve_2026_43499::leak::CollisionBuckets;
    using ghostlock::backend::cve_2026_43499::leak::futex_hash_context_init;
    using ghostlock::backend::cve_2026_43499::leak::MatchVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    constexpr std::size_t kTableSize = 256U;
    constexpr std::uint64_t kMm = 0xffff888000000000ULL;

    void the_rule_says_what_the_hash_says() {
        CollisionBuckets buckets;
        expect(futex_hash_context_init(&buckets.context, kTableSize) == 0, "the table size is usable");
        buckets.mm = kMm;
        const std::uint64_t first = 0x100000000000ULL;
        std::uint64_t twin = 0U;
        std::uint64_t other = 0U;
        for (std::uint64_t address = first + 0x1000ULL; address < 0x100001000000ULL; address += 0x1000ULL) {
            const bool same = buckets.bucket_of(address) == buckets.bucket_of(first);
            if (same && twin == 0U) {
                twin = address;
            }
            if (!same && other == 0U) {
                other = address;
            }
            if (twin != 0U && other != 0U) {
                break;
            }
        }
        expect(twin != 0U, "the hash collides at this table size, as it must");
        expect(other != 0U, "and produces buckets that differ");
        if (twin == 0U || other == 0U) {
            return;
        }
        expect(buckets.bucket_of(twin) == buckets.bucket_of(first), "the pair found shares a bucket");
        expect(buckets.bucket_of(other) != buckets.bucket_of(first), "the third does not");
        expect(buckets.bucket(first, first) == buckets.bucket_of(first),
               "the candidate rule answers with the candidate's own bucket");
        expect(buckets.bucket(other, first) == buckets.bucket_of(first),
               "whatever address it is asked about, which is the limit stated in the file");
        const std::uint64_t proofs[2] = {first, twin};
        expect(candidate_matches(buckets, proofs, 2U, first) == MatchVerdict::Matches,
               "so a candidate in the proofs' bucket is accepted");
        expect(candidate_matches(buckets, proofs, 2U, other) == MatchVerdict::Matches,
               "and so is one outside it: the check is agreement, not filtering");
        expect(candidate_matches(buckets, proofs, 1U, first) == MatchVerdict::TooFewToCompare,
               "while one address alone is refused: a collision needs something to collide with");
    }
}  // namespace

int main() {
    std::printf("collision buckets: the rule against the hash\n");
    the_rule_says_what_the_hash_says();
    if (g_failures != 0U) {
        std::printf("collision buckets: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("collision buckets: PASS (agrees with the hash, and states what it cannot decide)\n");
    return 0;
}
