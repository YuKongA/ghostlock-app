#pragma once
/** @file backend/cve_2026_43499/leak/collision_count.hpp
 *  How many of the armed addresses agree with a candidate, and whether that is enough.
 *
 *  The agreement check next door asks for one answer and gets one for any candidate; it is a
 *  necessary condition and nothing more. What tells the object apart is how many of the armed
 *  addresses put the candidate in the same bucket they are in. At a table of two hundred and
 *  fifty-six buckets a random address agrees with about one in two hundred and fifty-six of
 *  them, so requiring four independent agreements is a rare event and the rarer the better:
 *  the threshold is the signal. That threshold is the profile's collision count, which is why
 *  the key exists; it is not a retry budget.
 *
 *  Both functions are arithmetic over a bucket rule the caller supplies, so the rule tested
 *  next door and the counting tested here compose into what the search actually accepts.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** A rule that can say which bucket one address is in. */
    template <class Buckets>
    concept AddressBuckets = requires(const Buckets &buckets, std::uint64_t address) {
        { buckets.bucket_of(address) } -> std::convertible_to<std::uint64_t>;
    };

    /** How many of the armed addresses sit in the candidate's bucket. */
    template <AddressBuckets Buckets>
    [[nodiscard]] constexpr std::size_t count_bucket_agreements(const Buckets &buckets,
                                                                const std::uint64_t *addresses,
                                                                std::size_t count,
                                                                std::uint64_t candidate) noexcept {
        if (addresses == nullptr || count == 0U) {
            return 0U;
        }
        const std::uint64_t wanted = buckets.bucket_of(candidate);
        std::size_t agreed = 0U;
        for (std::size_t i = 0U; i < count; ++i) {
            if (buckets.bucket_of(addresses[i]) == wanted) {
                ++agreed;
            }
        }
        return agreed;
    }

    /** Whether that many agreements is enough to call it the object. A threshold of zero is
     *  refused rather than accepted: a rule that accepts everything is not a rule, and the
     *  failure mode it hides -- answering with the first candidate in the range -- is exactly
     *  what this counting exists to prevent. */
    [[nodiscard]] constexpr bool enough_agreements(std::size_t agreed, std::size_t needed) noexcept {
        return needed > 0U && agreed >= needed;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
