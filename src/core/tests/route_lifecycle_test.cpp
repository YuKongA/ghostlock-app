/** @file tests/route_lifecycle_test.cpp
 *  The route lifecycle's order, and what a refusal may not do.
 *
 *  Sections: 1. the happy path visits each stage once; 2. a step that does not
 *  follow from the current stage is refused and changes nothing; 3. over every
 *  pair of stage and step, a refusal never moves the stage and destroy is always
 *  legal -- the two properties the callers rely on.
 */
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/route/lifecycle.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::route::lifecycle_verdict_name;
    using ghostlock::backend::cve_2026_43499::route::LifecycleVerdict;
    using ghostlock::backend::cve_2026_43499::route::RouteStage;
    using ghostlock::backend::cve_2026_43499::route::route_stage_name;
    using ghostlock::backend::cve_2026_43499::route::route_step;
    using ghostlock::backend::cve_2026_43499::route::RouteStep;
    using ghostlock::backend::cve_2026_43499::route::route_step_name;
    using ghostlock::backend::cve_2026_43499::route::stage_is_settled;

    constexpr RouteStage kStages[] = {RouteStage::Idle, RouteStage::Prepared, RouteStage::Running,
                                      RouteStage::Disarmed, RouteStage::Destroyed};
    constexpr RouteStep kSteps[] = {RouteStep::Prepare, RouteStep::Execute, RouteStep::Disarm,
                                    RouteStep::Destroy};

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /* ---- 1: the happy path ---- */
    void the_happy_path_holds() {
        RouteStage stage = RouteStage::Idle;
        stage = route_step(stage, RouteStep::Prepare).stage;
        expect(stage == RouteStage::Prepared, "prepare leads to prepared");
        stage = route_step(stage, RouteStep::Execute).stage;
        expect(stage == RouteStage::Running, "execute leads to running");
        stage = route_step(stage, RouteStep::Disarm).stage;
        expect(stage == RouteStage::Disarmed, "disarm leads to disarmed");
        stage = route_step(stage, RouteStep::Destroy).stage;
        expect(stage == RouteStage::Destroyed, "destroy leads to destroyed");
        expect(stage_is_settled(stage), "a destroyed route is settled");
        expect(stage_is_settled(RouteStage::Idle), "and so is one that never ran");
        expect(!stage_is_settled(RouteStage::Running), "a running route is not settled");
    }

    /* ---- 2: a refusal changes nothing ---- */
    void illegal_steps_are_refused() {
        const auto execute_from_idle = route_step(RouteStage::Idle, RouteStep::Execute);
        expect(execute_from_idle.verdict == LifecycleVerdict::OutOfOrder,
               "executing before preparing is out of order");
        expect(std::string_view(lifecycle_verdict_name(execute_from_idle.verdict)) ==
                   std::string_view("order-violation"),
               "and it is named order-violation");
        expect(execute_from_idle.stage == RouteStage::Idle, "and the stage did not move");
        expect(route_step(RouteStage::Idle, RouteStep::Disarm).verdict == LifecycleVerdict::OutOfOrder,
               "disarming a route that never prepared is out of order");
        expect(route_step(RouteStage::Prepared, RouteStep::Prepare).verdict ==
               LifecycleVerdict::OutOfOrder,
               "preparing twice is out of order");
        expect(route_step(RouteStage::Disarmed, RouteStep::Prepare).verdict ==
               LifecycleVerdict::OutOfOrder,
               "a settled route does not start again");
        expect(route_step(RouteStage::Prepared, RouteStep::Execute).verdict == LifecycleVerdict::Ok,
               "and a prepared route may run");
        expect(route_step(RouteStage::Prepared, RouteStep::Disarm).verdict == LifecycleVerdict::Ok,
               "or be taken back down without running");
    }

    /* ---- 3: the two properties, over every pair ---- */
    void the_properties_hold_everywhere() {
        for (const RouteStage stage : kStages) {
            for (const RouteStep step : kSteps) {
                const auto result = route_step(stage, step);
                if (result.verdict != LifecycleVerdict::Ok) {
                    expect(result.stage == stage, "a refusal never moves the stage");
                }
                if (step == RouteStep::Destroy) {
                    expect(result.verdict == LifecycleVerdict::Ok, "destroy is always legal");
                    expect(result.stage == RouteStage::Destroyed, "and always ends destroyed");
                }
                if (stage == RouteStage::Destroyed && step != RouteStep::Destroy) {
                    expect(result.verdict == LifecycleVerdict::Destroyed,

                           "nothing but destroy works on a destroyed route");
                    expect(std::string_view(lifecycle_verdict_name(result.verdict)) ==
                               std::string_view("order-violation"),
                           "and that too is an order violation");
                }
                expect(std::string_view(route_stage_name(stage)) != std::string_view("unknown"),
                       "every stage has a name");
            }
            expect(std::string_view(route_step_name(RouteStep::Prepare)) == std::string_view("prepare"),
                   "the steps keep their names");
        }
        bool reached_destroyed = false;
        for (const RouteStage stage : kStages) {
            if (route_step(stage, RouteStep::Destroy).stage == RouteStage::Destroyed) {
                reached_destroyed = true;
            }
        }
        expect(reached_destroyed, "destroy reaches the destroyed stage from somewhere");
    }
}  // namespace

int main() {
    std::printf("route lifecycle: happy path\n");
    the_happy_path_holds();
    std::printf("route lifecycle: illegal steps\n");
    illegal_steps_are_refused();
    std::printf("route lifecycle: exhaustive\n");
    the_properties_hold_everywhere();
    if (g_failures != 0U) {
        std::printf("route lifecycle: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("route lifecycle: PASS (order enforced + refusals inert + destroy always legal)\n");
    return 0;
}
