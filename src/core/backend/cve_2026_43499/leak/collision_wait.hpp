#pragma once
/** @file backend/cve_2026_43499/leak/collision_wait.hpp
 *  How many pokes a collision search waits for, and how many it will look for.
 *
 *  The search forks children that each hammer one address, and the parent waits for
 *  the whole cohort to have poked as many times as the table can take: the hash table
 *  size times the collisions, times a factor that makes the cohort keep going long
 *  enough to be seen. The factor is mechanism and comes from the retired tree; the two
 *  counts are the search's own inputs.
 *
 *  The pool limit is the second rule and it is about how many collisions to look for:
 *  the search wants one fewer than it has addresses, because the first address is what
 *  the rest are compared against, but a caller that asks for a larger pool gets it. The
 *  two numbers cannot disagree silently -- taking the smaller would quietly search for
 *  less than was asked, and the run would look like a search that found nothing.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How many times the cohort pokes past what the table holds: mechanism, from the
     *  retired tree, which named it MULITPLE. */
    inline constexpr std::size_t kPokeMultiple = 4U;

    /** How many times every address of the cohort has to be poked before the parent stops
     *  waiting. Zero when the inputs cannot produce one, which the caller must refuse. */
    [[nodiscard]] constexpr std::size_t total_futex_pokes(std::size_t table_size,
                                                          std::size_t collisions) noexcept {
        if (table_size == 0U || collisions == 0U) {
            return 0U;
        }
        if (collisions > (SIZE_MAX / kPokeMultiple)) {
            return 0U;
        }
        const std::size_t scaled = collisions * kPokeMultiple;
        if (table_size > (SIZE_MAX / scaled)) {
            return 0U;
        }
        return table_size * scaled;
    }

    /** How many collisions the search looks for: one fewer than it has addresses, since
     *  the first is the reference, or more if the caller asked for more. */
    [[nodiscard]] constexpr std::size_t collision_pool_limit(std::size_t collisions,
                                                             std::size_t requested) noexcept {
        if (collisions == 0U) {
            return 0U;
        }
        const std::size_t wanted = collisions - 1U;
        return requested < wanted ? wanted : requested;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
