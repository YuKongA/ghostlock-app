/** @file tests/collision_wait_test.cpp
 *  How long the parent waits, and how many collisions it looks for.
 *
 *  Two rules that were spelled out in the retired tree and are worth their own file:
 *  the cohort has to poke the table size times the collisions times a factor, and the
 *  search wants one fewer collision than it has addresses because the first address is
 *  the reference. Both take the larger of what they want and what they were asked for,
 *  and neither is allowed to quietly return less: a search that looks for fewer
 *  collisions than requested is one that will look like it found nothing.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "backend/cve_2026_43499/leak/collision_wait.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::collision_pool_limit;
    using ghostlock::backend::cve_2026_43499::leak::kPokeMultiple;
    using ghostlock::backend::cve_2026_43499::leak::total_futex_pokes;

    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_pokes_are_the_table_times_the_cohort() {
        expect(kPokeMultiple == 4U, "the factor is the retired tree own");
        expect(total_futex_pokes(2048U, 2U) == 16384U, "eight processors, two collisions, four times over");
        expect(total_futex_pokes(256U, 8U) == 8192U, "and the arithmetic follows the table");
        expect(total_futex_pokes(0U, 2U) == 0U, "a table of no size cannot be poked");
        expect(total_futex_pokes(2048U, 0U) == 0U, "nor can no collisions");
        expect(total_futex_pokes(kMax, 2U) == 0U, "and an overflowing product is refused, not wrapped");
        expect(total_futex_pokes(2U, kMax) == 0U, "including one that overflows on the way in");
    }

    void the_pool_wants_one_fewer_than_the_addresses() {
        expect(collision_pool_limit(2U, 0U) == 1U,
               "two addresses want one collision: the first is what the second is compared against");
        expect(collision_pool_limit(3U, 0U) == 2U, "three addresses want two");
        expect(collision_pool_limit(2U, 5U) == 5U, "a caller asking for more gets more");
        expect(collision_pool_limit(1U, 0U) == 0U, "one address wants none, and gets none");
        expect(collision_pool_limit(0U, 4U) == 0U, "and no addresses is no pool at all");
    }
}  // namespace

int main() {
    std::printf("collision wait: the pokes\n");
    the_pokes_are_the_table_times_the_cohort();
    std::printf("collision wait: the pool\n");
    the_pool_wants_one_fewer_than_the_addresses();
    if (g_failures != 0U) {
        std::printf("collision wait: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("collision wait: PASS (how many pokes, how many collisions)\n");
    return 0;
}
