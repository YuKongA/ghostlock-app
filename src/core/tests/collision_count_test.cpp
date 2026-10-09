/** @file tests/collision_count_test.cpp
 *  The counting rule against the kernel's own hash, on sets built by observing the buckets.
 *
 *  The test builds a set of armed addresses that deliberately share one bucket and one that does
 *  not, then asks how a candidate in each bucket counts. It also asserts the case the threshold
 *  exists for: a candidate in an empty bucket agrees with none of them, which is what a random
 *  address looks like and what has to be refused.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/collision_count.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::CollisionBuckets;
    using ghostlock::backend::cve_2026_43499::leak::count_bucket_agreements;
    using ghostlock::backend::cve_2026_43499::leak::enough_agreements;
    using ghostlock::backend::cve_2026_43499::leak::futex_hash_context_init;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    constexpr std::size_t kTableSize = 256U;
    constexpr std::uint64_t kMm = 0xffff888000000000ULL;

    void counting_separates_the_object_from_a_stranger() {
        CollisionBuckets buckets;
        expect(futex_hash_context_init(&buckets.context, kTableSize) == 0, "the table is usable");
        buckets.mm = kMm;
        /* Four armed addresses, three of them observed to share a bucket. */
        std::uint64_t armed[4] = {0U, 0U, 0U, 0U};
        armed[0] = 0x100000000000ULL;
        const std::uint64_t wanted = buckets.bucket_of(armed[0]);
        std::size_t found = 1U;
        std::uint64_t stranger = 0U;
        /* Scan the whole window rather than stopping at the first stranger: the first version
         *  of this test stopped as soon as it found one, which is before it had the three the
         *  case needs. */
        for (std::uint64_t at = armed[0] + 0x1000ULL; at < armed[0] + 0x400000ULL; at += 0x1000ULL) {
            if (buckets.bucket_of(at) == wanted) {
                if (found < 3U) {
                    armed[found] = at;
                    ++found;
                }
            } else if (stranger == 0U) {
                stranger = at;
            }
        }
        armed[3] = stranger;
        expect(found == 3U, "three addresses in one bucket were found");
        expect(stranger != 0U && buckets.bucket_of(stranger) != wanted, "and one outside it");
        expect(count_bucket_agreements(buckets, armed, 4U, armed[0]) == 3U,
               "a candidate in that bucket agrees with the three that live there");
        expect(count_bucket_agreements(buckets, armed, 4U, stranger) == 1U,
               "and one outside agrees only with the stranger, which shares its bucket");
        expect(enough_agreements(3U, 4U) == false, "three of four is not enough at a threshold of four");
        expect(enough_agreements(3U, 3U), "but it is at three");
        expect(enough_agreements(0U, 0U) == false, "a threshold of zero is refused, not satisfied");
        expect(count_bucket_agreements(buckets, nullptr, 4U, armed[0]) == 0U,
               "and no addresses agree with nothing");
    }
}  // namespace

int main() {
    std::printf("collision count: three of four is not four\n");
    counting_separates_the_object_from_a_stranger();
    if (g_failures != 0U) {
        std::printf("collision count: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("collision count: PASS (the count is what tells the object apart)\n");
    return 0;
}
