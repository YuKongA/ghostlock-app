/** @file backend/cve_2026_43499/memory/prepare_plan.cpp
 *  Turning sizes into a plan: the order from its table, the arithmetic here.
 */
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    PrepareVerdict build_prepare_plan(const PrepareInputs &inputs, PreparePlan &out) noexcept {
        out = PreparePlan{};
        /* The phases come from the table, not from a copy of it: what a plan holds is
         *  a view of the order the mechanism is pinned to. */
        const PrepareStep *steps = kPrepareOrder;
        const std::size_t step_count = kPrepareStepCount;
        /* A preparation with no spray has nothing to reclaim and a search asked for
         *  no collisions will wait for something that cannot arrive: both are sizes
         *  nobody can act on rather than plans to be run hopefully. */
        if (inputs.pre_groups == 0U || inputs.spray_groups == 0U || inputs.post_groups == 0U ||
            inputs.snitch_collisions == 0U) {
            return PrepareVerdict::InputsInvalid;
        }
        if (inputs.region_size == 0U || inputs.object_size == 0U) {
            return PrepareVerdict::GeometryInvalid;
        }
        if (inputs.region_size % inputs.object_size != 0U) {
            return PrepareVerdict::GeometryInvalid;
        }
        const std::uint64_t stride = inputs.region_size / inputs.object_size;
        /* A stride that cannot be held is not actionable either. The division is exact
         *  by the check above, so this only fires where the quotient does not fit. */
        if (stride == 0U || stride > static_cast<std::uint64_t>(SIZE_MAX)) {
            return PrepareVerdict::GeometryInvalid;
        }
        out.steps = steps;
        out.step_count = step_count;
        out.pre_groups = inputs.pre_groups;
        out.spray_groups = inputs.spray_groups;
        out.post_groups = inputs.post_groups;
        out.leak_groups = inputs.leak_groups;
        out.snitch_collisions = inputs.snitch_collisions;
        out.objects_per_region = static_cast<std::size_t>(stride);
        return PrepareVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
