/** @file tests/reclaim_plan_test.cpp
 *  Which descriptors a page preparation gives back, and in what order.
 *
 *  The order is the mechanism: the retired tree closed the pre-spray children, then
 *  the post-spray ones except the last, then one spray descriptor per slab, then the
 *  shaping pair, and last the leak memfd -- because closing that one is what drops
 *  the reference the spray depends on. The test walks a worked example and checks
 *  every step in place, then the shapes where a group is empty or a count cannot be
 *  acted on.
 */
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "memory/reclaim_plan.hpp"

namespace {
    using ghostlock::memory::kMaxReclaimSteps;
    using ghostlock::memory::plan_reclaim;
    using ghostlock::memory::ReclaimCounts;
    using ghostlock::memory::ReclaimGroup;
    using ghostlock::memory::ReclaimStep;
    using ghostlock::memory::reclaim_verdict_name;
    using ghostlock::memory::ReclaimVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_order_is_the_one_the_tree_used() {
        ReclaimCounts counts;
        counts.pre = 2U;
        counts.post = 3U;
        counts.spray = 4U;
        counts.mm_objs_per_slab = 2U;
        counts.shaped = true;
        ReclaimStep steps[kMaxReclaimSteps] = {};
        std::size_t written = 0U;
        expect(plan_reclaim(counts, steps, kMaxReclaimSteps, written) == ReclaimVerdict::Ok,
               "the plan is produced");
        expect(written == 9U, "with the nine steps the counts imply");
        const ReclaimGroup expected_groups[9U] = {
            ReclaimGroup::Pre,   ReclaimGroup::Pre,   ReclaimGroup::Post,  ReclaimGroup::Post,
            ReclaimGroup::Spray, ReclaimGroup::Spray, ReclaimGroup::Shaping,
            ReclaimGroup::Shaping, ReclaimGroup::Leak};
        const std::size_t expected_index[9U] = {0U, 1U, 0U, 1U, 0U, 2U, 0U, 1U, 0U};
        for (std::size_t i = 0U; i < 9U && i < written; ++i) {
            expect(steps[i].group == expected_groups[i] && steps[i].index == expected_index[i],
                   "every step is the group and index the retired tree used");
        }
        /* The last post-spray descriptor is NOT given back: it is the one the reclaim
         *  still needs when the writes happen. */
        expect(steps[written - 2U].group == ReclaimGroup::Shaping &&
                   steps[written - 1U].group == ReclaimGroup::Leak,
               "and the leak descriptor is closed last of all");
    }

    void the_shapes_and_the_refusals() {
        ReclaimStep steps[kMaxReclaimSteps] = {};
        std::size_t written = 0U;
        ReclaimCounts bare;
        bare.shaped = false;
        expect(plan_reclaim(bare, steps, kMaxReclaimSteps, written) == ReclaimVerdict::Ok && written == 1U,
               "a preparation with no children still returns the leak descriptor");
        ReclaimCounts single_post;
        single_post.post = 1U;
        single_post.shaped = true;
        expect(plan_reclaim(single_post, steps, kMaxReclaimSteps, written) == ReclaimVerdict::Ok &&
                   written == 3U,
               "a single post descriptor is kept, not given back");
        ReclaimCounts no_slabs;
        no_slabs.spray = 4U;
        expect(plan_reclaim(no_slabs, steps, kMaxReclaimSteps, written) == ReclaimVerdict::CountsInvalid,
               "a spray the slab size cannot walk is not a plan");
        expect(std::string_view(reclaim_verdict_name(ReclaimVerdict::CountsInvalid)) ==
                   std::string_view("param-invalid"),
               "and it is named as a bad parameter rather than a missing one");
        ReclaimCounts many;
        many.pre = kMaxReclaimSteps;
        expect(plan_reclaim(many, steps, 4U, written) == ReclaimVerdict::NoRoom && written == 0U,
               "a plan that does not fit leaves nothing half-written");
        expect(plan_reclaim(many, nullptr, 0U, written) == ReclaimVerdict::NoRoom,
               "and no array at all is refused too");
    }
}  // namespace

int main() {
    std::printf("reclaim plan: the order\n");
    the_order_is_the_one_the_tree_used();
    std::printf("reclaim plan: shapes and refusals\n");
    the_shapes_and_the_refusals();
    if (g_failures != 0U) {
        std::printf("reclaim plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("reclaim plan: PASS (the tree's order + empty shapes + named refusals)\n");
    return 0;
}
