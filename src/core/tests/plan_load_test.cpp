/** @file tests/plan_load_test.cpp
 *  Loading a plan: one delivered script, one sandbox, one run.
 *
 *  This is the step the plan spec puts second -- after the sandbox and the
 *  handles, before the executors -- and it is a composition, so what it has to
 *  show is that the parts meet: a script runs inside the sandbox s budgets, its
 *  calls arrive at the sink, and the sink that keeps them is the staged plan. The
 *  refusals are checked to be the sandbox s own names, because a second vocabulary
 *  here would mean two places to change when a name changes.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/plan_load.hpp"
#include "script/staged_plan.hpp"

namespace {
    using ghostlock::contract::kSurface;
    using ghostlock::contract::kSurfaceCount;
    using ghostlock::script::CallSink;
    using ghostlock::script::CallView;
    using ghostlock::script::CfgField;
    using ghostlock::script::load_plan;
    using ghostlock::script::PlanLoadInput;
    using ghostlock::script::ScriptBudget;
    using ghostlock::script::ScriptRejection;
    using ghostlock::script::script_rejection_name;
    using ghostlock::script::StagedPlan;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The sink keeps what the script called, in the staged plan. */
    void record_call(void *context, const CallView *view) {
        auto *plan = static_cast<StagedPlan *>(context);
        (void) plan->record(*view);
    }

    void note_error(void *context, const char *named_code) {
        auto *plan = static_cast<StagedPlan *>(context);
        (void) plan->refuse(std::string_view(named_code));
    }

    /** The budget a real run would inject; only what the sandbox needs here. */
    ScriptBudget budget() {
        ScriptBudget made;
        made.max_instructions = 200000U;
        made.max_bytes = 1U << 20U;
        made.max_stack_depth = 32U;
        made.max_script_bytes = 64U * 1024U;
        return made;
    }

    /** The one field the sample script reads. */
    constexpr CfgField kCfg[] = {{"probe", 7U}};

    ScriptRejection load(std::string_view script, StagedPlan &plan) {
        PlanLoadInput input;
        input.budget = budget();
        input.cfg = kCfg;
        input.cfg_count = 1U;
        input.sink.context = &plan;
        input.sink.on_op = record_call;
        input.sink.on_error = note_error;
        return load_plan(script, input);
    }

    void a_plan_loads() {
        StagedPlan plan;
        const ScriptRejection rejection = load("surface.backend(cfg.probe)", plan);
        expect(rejection == ScriptRejection::None, "a script that only calls the surface loads");
        expect(plan.ok() && plan.size() == 1U,
               "and what it called is in the staged plan, which is the composition working");
        std::size_t backend_row = kSurfaceCount;
        for (std::size_t i = 0U; i < kSurfaceCount; ++i) {
            if (std::string_view(kSurface[i].name) == std::string_view("backend")) {
                backend_row = i;
            }
        }
        expect(backend_row != kSurfaceCount, "the surface declares the row the script named");
        expect(plan.size() == 1U && plan.at(0U).surface_index == backend_row,
               "and the recorded call names that row");
        expect(plan.at(0U).arg_count == 1U && plan.at(0U).args[0U].u64 == 7U,
               "with the argument the cfg snapshot supplied");
    }

    void what_is_refused() {
        StagedPlan plan;
        expect(load("surface.backend(", plan) == ScriptRejection::PlanSyntax,
               "a script that does not parse is refused as a syntax error");
        expect(load("surface.not_a_row()", plan) == ScriptRejection::SurfaceUnknownName,
               "a name the surface does not declare is refused as unknown");
        expect(load("", plan) == ScriptRejection::PlanSyntax,
               "and an empty script is not a plan");
        /* A budget with no values at all: the mechanism is not configured, and that
         *  must not be reported as a script that overran -- the two failures send
         *  whoever reads them after different things. */
        PlanLoadInput unconfigured;
        unconfigured.sink.context = &plan;
        unconfigured.sink.on_op = record_call;
        unconfigured.sink.on_error = note_error;
        expect(load_plan("surface.backend(cfg.probe)", unconfigured) ==
                   ScriptRejection::TuningMissing,
               "an unconfigured sandbox is refused as a missing tuning value");
        expect(std::string_view(script_rejection_name(ScriptRejection::TuningMissing)) ==
                   std::string_view("tuning-missing"),
               "under that name rather than budget-exceeded");
        StagedPlan looped;
        PlanLoadInput input;
        input.budget = budget();
        input.budget.max_instructions = 10000U;
        input.sink.context = &looped;
        input.sink.on_op = record_call;
        input.sink.on_error = note_error;
        const ScriptRejection overrun = load_plan("while true do end", input);
        expect(overrun == ScriptRejection::BudgetExceeded,
               "a script that runs past its instruction budget is refused by that name");
    }
}  // namespace

int main() {
    std::printf("plan load: a plan that loads\n");
    a_plan_loads();
    std::printf("plan load: what is refused\n");
    what_is_refused();
    if (g_failures != 0U) {
        std::printf("plan load: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("plan load: PASS (runs + records + refuses by the sandbox's own names)\n");
    return 0;
}
