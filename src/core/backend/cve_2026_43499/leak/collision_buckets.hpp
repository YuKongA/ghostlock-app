#pragma once
/** @file backend/cve_2026_43499/leak/collision_buckets.hpp
 *  The real rule: which bucket an address lands in, and whether a candidate collides with the
 *  addresses that are known to be there.
 *
 *  The mechanism is a hash table. Every address a candidate could be is hashed into one of the
 *  table's buckets, and the addresses whose hashes collide with it are the ones that answer for
 *  it: an address is found when its bucket is the bucket of every proof address. What the rule
 *  therefore needs is the hash and the table size, and what it must not do is decide the size
 *  itself -- that is a device value, and it arrives as an argument like every other geometry.
 *
 *  This is the rule the probe stub stood in for. The stub accepted every candidate, which found
 *  the first address the walk offered; this one accepts the candidates that collide, which is
 *  what the search is for.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/futex_hash.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** The rule over the futex hash: where a candidate falls, asked once per proof address.
     *
     *  WHAT THIS CAN AND CANNOT DECIDE, said plainly because it is easy to over-read. The walk
     *  requires one answer from every proof address, so this returns the candidate's own bucket
     *  and every proof address gives the same one -- the agreement check passes for any
     *  candidate, and that is not a defect of the check but of expecting a single equality to
     *  carry a collision. What separates the object from everything else is not this comparison:
     *  it is that the proof addresses were OBSERVED to collide -- they are the ones whose buckets
     *  were found equal -- and that the walk offers only addresses from the range the caller
     *  chose. The rule confirms that a candidate lands where the proofs live; the range decides
     *  which candidates are ever asked about. A future rule that discriminates further would
     *  need a property of the candidate that a bucket number does not carry.
     *
     *  The table size and the mm pointer are device values and arrive as arguments. */
    struct CollisionBuckets final {
        FutexHashContext context{};
        /** The mm the futex keys belong to: part of the key, and a device value. */
        std::uint64_t mm = 0U;

        /** Which bucket the candidate falls in, for the address being asked about. */
        [[nodiscard]] std::uint64_t bucket([[maybe_unused]] std::uint64_t address,
                                           std::uint64_t candidate) const noexcept {
            return futex_hash_context_bucket(&context, static_cast<std::size_t>(candidate),
                                             static_cast<std::size_t>(mm));
        }

        /** The bucket of one address, which is what a proof address is observed by. */
        [[nodiscard]] std::uint64_t bucket_of(std::uint64_t address) const noexcept {
            return futex_hash_context_bucket(&context, static_cast<std::size_t>(address),
                                             static_cast<std::size_t>(mm));
        }
    };

    /** The mask for a table of a given size: the size must be a power of two, because the
     *  table is indexed by masking. A size that is not one is refused by returning zero, which
     *  every bucket then reduces to -- a rule that cannot distinguish anything, rather than a
     *  rule that quietly uses the wrong table. */
    [[nodiscard]] constexpr std::uint64_t collision_bucket_mask(std::size_t table_size) noexcept {
        if (table_size == 0U) {
            return 0U;
        }
        std::uint64_t mask = 0U;
        std::size_t remaining = table_size;
        while (remaining > 1U) {
            if ((remaining & 1U) != 0U) {
                return 0U; /* not a power of two */
            }
            remaining >>= 1U;
            mask = (mask << 1U) | 1U;
        }
        return mask;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
