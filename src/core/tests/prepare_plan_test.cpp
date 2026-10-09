/** @file tests/prepare_plan_test.cpp
 *  Turning sizes into a plan: the order it reads, and the number it derives.
 *
 *  Every size here is an input chosen by the test, not a value the mechanism owns,
 *  so the cases are about the arithmetic and the refusals rather than about one
 *  device. The plan is checked to carry the phases from the table that owns them and
 *  to derive the stride by dividing the region by the object in it.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/memory/prepare_plan.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::build_prepare_plan;
    using ghostlock::backend::cve_2026_43499::memory::kPrepareOrder;
    using ghostlock::backend::cve_2026_43499::memory::kPrepareStepCount;
    using ghostlock::backend::cve_2026_43499::memory::PrepareInputs;
    using ghostlock::backend::cve_2026_43499::memory::PreparePlan;
    using ghostlock::backend::cve_2026_43499::memory::prepare_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::PrepareVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    PrepareInputs usable() {
        PrepareInputs inputs;
        inputs.pre_groups = 4U;
        inputs.spray_groups = 8U;
        inputs.post_groups = 3U;
        inputs.leak_groups = 1U;
        inputs.snitch_collisions = 2U;
        inputs.region_size = 32768U;
        inputs.object_size = 512U;
        return inputs;
    }

    void a_plan_from_sizes() {
        PreparePlan plan;
        expect(build_prepare_plan(usable(), plan) == PrepareVerdict::Ok, "a usable set builds a plan");
        expect(plan.steps == kPrepareOrder && plan.step_count == kPrepareStepCount,
               "and the plan reads the order from the table that owns it, not a copy");
        expect(plan.pre_groups == 4U && plan.spray_groups == 8U && plan.post_groups == 3U,
               "with the sizes it was given");
        expect(plan.snitch_collisions == 2U, "and the collisions the search was asked for");
        expect(plan.objects_per_region == 64U,
               "and the stride, which is the region divided by one object in it");
    }

    void what_cannot_be_planned() {
        PreparePlan plan;
        PrepareInputs no_pre = usable();
        no_pre.pre_groups = 0U;
        expect(build_prepare_plan(no_pre, plan) == PrepareVerdict::InputsInvalid,
               "a preparation with no pre-spray children is not a plan");
        PrepareInputs no_spray = usable();
        no_spray.spray_groups = 0U;
        expect(build_prepare_plan(no_spray, plan) == PrepareVerdict::InputsInvalid,
               "nor one with nothing to spray");
        PrepareInputs no_collisions = usable();
        no_collisions.snitch_collisions = 0U;
        expect(build_prepare_plan(no_collisions, plan) == PrepareVerdict::InputsInvalid,
               "nor a search asked for no collision, which could only wait forever");
        expect(std::string_view(prepare_verdict_name(PrepareVerdict::InputsInvalid)) ==
                   std::string_view("param-invalid"),
               "refused as a bad parameter");
        PrepareInputs ragged = usable();
        ragged.object_size = 300U;
        expect(build_prepare_plan(ragged, plan) == PrepareVerdict::GeometryInvalid,
               "a region that does not divide by its object has no whole stride");
        expect(std::string_view(prepare_verdict_name(PrepareVerdict::GeometryInvalid)) ==
                   std::string_view("geometry-missing"),
               "and that is the geometry refusal, not the parameter one");
        PrepareInputs no_object = usable();
        no_object.object_size = 0U;
        expect(build_prepare_plan(no_object, plan) == PrepareVerdict::GeometryInvalid,
               "an object of no size has no stride either");
        expect(plan.steps == nullptr && plan.step_count == 0U,
               "and a refusal hands over nothing to walk");
    }
}  // namespace

int main() {
    std::printf("prepare plan: from sizes\n");
    a_plan_from_sizes();
    std::printf("prepare plan: what cannot be planned\n");
    what_cannot_be_planned();
    if (g_failures != 0U) {
        std::printf("prepare plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("prepare plan: PASS (order read, sizes carried, stride derived, refusals named)\n");
    return 0;
}
