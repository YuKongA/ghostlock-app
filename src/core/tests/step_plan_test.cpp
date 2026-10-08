/* Host test for the canonical step-plan normalizer (step-queue design doc
 * sections 4.1-4.3, 5-Q1/Q2/Q3; batch M1.1).
 *
 * Every declaration-time rule of the design has a case here, and every case is
 * a guard that has been shown to fail (falsification log in the M1.1 report).
 * The `reason=` tokens are pinned verbatim because the runtime diagnostics
 * (section 10.1) print them. */

#include "contract/step_plan.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string_view>

using ghostlock::contract::BackendKind;
using ghostlock::contract::CanonicalPlan;
using ghostlock::contract::CombinationKind;
using ghostlock::contract::CountermeasureStage;
using ghostlock::contract::normalize_step_queue;
using ghostlock::contract::PlanError;
using ghostlock::contract::plan_error_reason;
using ghostlock::contract::PlanResult;
using ghostlock::contract::PlanVerdict;
using ghostlock::contract::QueueElement;
using ghostlock::contract::QueueInput;
using ghostlock::contract::SeamSlot;
using ghostlock::contract::StepId;

namespace {
    constexpr BackendKind k43499 = BackendKind::Cve2026_43499;
    constexpr BackendKind k43284 = BackendKind::Cve2026_43284;

    constexpr QueueElement step_element(std::string_view token) {
        QueueElement element;
        element.step = token;
        return element;
    }

    constexpr QueueElement seam_element(std::string_view stage,
                                        std::string_view type = "plugin") {
        QueueElement element;
        element.seam = type;
        element.stage = stage;
        return element;
    }

    PlanResult normalize(BackendKind backend, std::string_view route,
                         std::span<const QueueElement> elements,
                         bool experimental_declared = false) {
        QueueInput input;
        input.backend = backend;
        input.route = route;
        input.experimental_declared = experimental_declared;
        input.elements = elements;
        return normalize_step_queue(input);
    }

    int failures = 0;

    void expect_error(const char *what, const PlanResult &result, PlanError expected) {
        if (result.error != expected) {
            std::fprintf(stderr, "step_plan_test: %s: expected reason=%s, got reason=%s\n",
                         what, std::string(plan_error_reason(expected)).c_str(),
                         std::string(plan_error_reason(result.error)).c_str());
            ++failures;
        }
    }

    const CanonicalPlan &expect_ok(const char *what, const PlanResult &result) {
        if (!result.ok()) {
            std::fprintf(stderr, "step_plan_test: %s: expected a plan, got reason=%s\n", what,
                         std::string(plan_error_reason(result.error)).c_str());
            ++failures;
        }
        return result.plan;
    }
} // namespace

int main() {
    /* ---- reason tokens are the printed contract (section 10.1) ---- */
    const struct { PlanError error; const char *reason; } kReasons[] = {
        {PlanError::None, "none"},
        {PlanError::EmptyQueue, "empty-queue"},
        {PlanError::NoSteps, "no-steps"},
        {PlanError::TooManySteps, "too-many-steps"},
        {PlanError::TooManySeams, "too-many-seams"},
        {PlanError::ElementNotObject, "queue-element-not-object"},
        {PlanError::ElementUnknownKey, "queue-element-unknown-key"},
        {PlanError::ParamsReserved, "params-reserved-for-future-step-parameters"},
        {PlanError::StepAndSeam, "queue-element-step-and-seam"},
        {PlanError::MissingStepOrSeam, "queue-element-missing-step-or-seam"},
        {PlanError::StepRouteNotAllowed, "step-route-not-allowed"},
        {PlanError::UnknownStepToken, "unknown-step-token"},
        {PlanError::CrossBackendStep, "cross-backend-step"},
        {PlanError::DuplicateStep, "duplicate-step"},
        {PlanError::MissingDependency, "missing-dependency"},
        {PlanError::NotACanonicalPrefix, "not-a-canonical-prefix"},
        {PlanError::RouteRequired, "route-required"},
        {PlanError::RouteNotApplicable, "route-not-applicable"},
        {PlanError::RouteDuplicated, "route-duplicated"},
        {PlanError::UnknownRouteToken, "unknown-route-token"},
        {PlanError::UnknownSeamType, "unknown-seam-type"},
        {PlanError::UnknownSeamStage, "unknown-seam-stage"},
        {PlanError::SeamStageIllegal, "seam-stage-illegal"},
        {PlanError::ExperimentalNotDeclared, "experimental-not-declared"},
    };
    for (const auto &entry : kReasons) {
        if (plan_error_reason(entry.error) != entry.reason) {
            std::fprintf(stderr, "step_plan_test: reason token drift: %s\n", entry.reason);
            ++failures;
        }
    }

    /* ---- supported: the queue equals a device-verified preset ---- */
    const std::array<QueueElement, 3> w1w2w3 = {step_element("w1"), step_element("w2"),
                                                 step_element("w3")};
    const PlanResult full = normalize(k43499, "select_stack", w1w2w3);
    const CanonicalPlan &full_plan = expect_ok("w1_w3 + select_stack", full);
    assert(full_plan.verdict == PlanVerdict::Supported);
    assert(full_plan.preset == CombinationKind::PselectRootchild);
    assert(full_plan.step_count == 3U && full_plan.steps[2] == StepId::W3);
    assert(full_plan.seam_count == 0U);

    const std::array<QueueElement, 1> pagecache = {step_element("pagecache_write")};
    const CanonicalPlan &pc_plan = expect_ok("43284 pagecache_write",
                                             normalize(k43284, "", pagecache));
    assert(pc_plan.verdict == PlanVerdict::Supported);
    assert(pc_plan.preset == CombinationKind::Umh);
    assert(pc_plan.route == ghostlock::profile::RouteKind::None);

    const std::array<QueueElement, 2> w1w2 = {step_element("w1"), step_element("w2")};
    assert(expect_ok("w1_w2 + select_stack", normalize(k43499, "select_stack", w1w2))
                   .preset == CombinationKind::PselectShizuku);

    /* ---- seams: pre_spawn before the steps, post_terminal after them ---- */
    const std::array<QueueElement, 5> seamed = {seam_element("pre_spawn"), step_element("w1"),
                                                step_element("w2"), step_element("w3"),
                                                seam_element("post_terminal")};
    const CanonicalPlan &seamed_plan = expect_ok("seamed queue",
                                                 normalize(k43499, "select_stack", seamed));
    assert(seamed_plan.step_count == 3U && seamed_plan.seam_count == 2U);
    assert(seamed_plan.seams[0].stage == CountermeasureStage::PreSpawn);
    assert(seamed_plan.seams[0].position == 0U);
    assert(seamed_plan.seams[1].stage == CountermeasureStage::PostTerminal);
    assert(seamed_plan.seams[1].position == 4U);

    /* ---- experimental: declared request, computed verdict (U5) ---- */
    const std::array<QueueElement, 1> only_w1 = {step_element("w1")};
    expect_error("w1 without declaration", normalize(k43499, "select_stack", only_w1),
                 PlanError::ExperimentalNotDeclared);
    const CanonicalPlan &experimental =
            expect_ok("w1 declared experimental",
                      normalize(k43499, "select_stack", only_w1, true));
    assert(experimental.verdict == PlanVerdict::Experimental);
    assert(experimental.preset == CombinationKind::Unknown);
    /* U5 rule 2: declaring does NOT downgrade a verified preset. */
    const CanonicalPlan &declared_full = expect_ok(
            "declared but verified", normalize(k43499, "select_stack", w1w2w3, true));
    assert(declared_full.verdict == PlanVerdict::Supported);

    /* ---- route rules (5-Q2) ---- */
    expect_error("routed backend without route", normalize(k43499, "", w1w2w3),
                 PlanError::RouteRequired);
    expect_error("routed backend with an unknown route",
                 normalize(k43499, "bogus", w1w2w3), PlanError::UnknownRouteToken);
    expect_error("route-less backend with a route", normalize(k43284, "select_stack", pagecache),
                 PlanError::RouteNotApplicable);
    {
        QueueInput input;
        input.backend = k43499;
        input.route = "select_stack";
        input.route_duplicated = true;
        input.elements = w1w2w3;
        expect_error("duplicate route", normalize_step_queue(input), PlanError::RouteDuplicated);
    }
    {
        QueueElement element = step_element("w1");
        element.has_step_route = true;
        const std::array<QueueElement, 1> per_step = {element};
        expect_error("per-step route", normalize(k43499, "select_stack", per_step),
                     PlanError::StepRouteNotAllowed);
    }

    /* ---- element shape (5-Q1, U9, U10) ---- */
    {
        const std::array<QueueElement, 1> strings = {[]{
            QueueElement element;
            element.is_object = false;
            element.step = "w1";
            return element;
        }()};
        expect_error("bare-string element", normalize(k43284, "", strings),
                     PlanError::ElementNotObject);
    }
    {
        const std::array<QueueElement, 1> neither = {QueueElement{}};
        expect_error("object without step/seam", normalize(k43284, "", neither),
                     PlanError::MissingStepOrSeam);
    }
    {
        QueueElement element = step_element("w1");
        element.seam = "plugin";
        element.stage = "pre_spawn";
        const std::array<QueueElement, 1> both = {element};
        expect_error("step and seam together", normalize(k43499, "select_stack", both),
                     PlanError::StepAndSeam);
    }
    {
        QueueElement element = step_element("w1");
        element.has_params = true;
        const std::array<QueueElement, 1> params = {element};
        expect_error("reserved params key", normalize(k43499, "select_stack", params),
                     PlanError::ParamsReserved);
    }
    {
        QueueElement element = step_element("w1");
        element.unknown_key = "surprise";
        const std::array<QueueElement, 1> unknown = {element};
        expect_error("unknown key", normalize(k43499, "select_stack", unknown),
                     PlanError::ElementUnknownKey);
    }
    {
        QueueElement element = step_element("w1");
        element.stage = "pre_spawn";
        const std::array<QueueElement, 1> staged_step = {element};
        expect_error("stage on a step element", normalize(k43499, "select_stack", staged_step),
                     PlanError::ElementUnknownKey);
    }

    /* ---- step vocabulary ---- */
    {
        const std::array<QueueElement, 1> bogus = {step_element("w9")};
        expect_error("unknown step token", normalize(k43499, "select_stack", bogus),
                     PlanError::UnknownStepToken);
        expect_error("cross-backend step", normalize(k43499, "select_stack", pagecache),
                     PlanError::CrossBackendStep);
    }
    {
        const std::array<QueueElement, 2> duplicate = {step_element("w1"), step_element("w1")};
        expect_error("duplicate step", normalize(k43499, "select_stack", duplicate),
                     PlanError::DuplicateStep);
    }
    {
        const std::array<QueueElement, 2> gap = {step_element("w1"), step_element("w3")};
        expect_error("missing dependency", normalize(k43499, "select_stack", gap),
                     PlanError::MissingDependency);
    }
    {
        /* A sequence that is not the canonical prefix is refused even when every
         * dependency happens to be present. */
        const std::array<QueueElement, 1> not_a_prefix = {step_element("w2")};
        expect_error("not a canonical prefix",
                     normalize(k43499, "select_stack", not_a_prefix, true),
                     PlanError::MissingDependency);
    }

    /* ---- empty queues ---- */
    expect_error("empty element list", normalize(k43284, "", {}), PlanError::EmptyQueue);
    {
        const std::array<QueueElement, 1> seams_only = {seam_element("pre_spawn")};
        expect_error("seams without steps", normalize(k43284, "", seams_only),
                     PlanError::NoSteps);
    }

    /* ---- seam legality (5-Q3, R1) ---- */
    {
        const std::array<QueueElement, 2> wrong_stage = {seam_element("post_spawn"),
                                                         step_element("w1")};
        expect_error("post_spawn before the steps", normalize(k43499, "select_stack", wrong_stage),
                     PlanError::SeamStageIllegal);
    }
    {
        /* Seams alone are legal positions but not a plan. */
        const std::array<QueueElement, 2> both_seams = {seam_element("pre_spawn"),
                                                        seam_element("post_terminal")};
        expect_error("seams without steps", normalize(k43284, "", both_seams),
                     PlanError::NoSteps);
    }
    {
        const std::array<QueueElement, 3> middle = {step_element("w1"),
                                                    seam_element("pre_spawn"),
                                                    step_element("w2")};
        expect_error("seam inside the step range", normalize(k43499, "select_stack", middle),
                     PlanError::SeamStageIllegal);
    }
    {
        const std::array<QueueElement, 1> other_type = {seam_element("pre_spawn", "not_a_plugin")};
        expect_error("unknown seam type", normalize(k43284, "select_stack", other_type),
                     PlanError::RouteNotApplicable);
        expect_error("unknown seam type (routed backend)",
                     normalize(k43499, "select_stack", other_type), PlanError::UnknownSeamType);
    }
    {
        const std::array<QueueElement, 1> bad_stage = {seam_element("whenever")};
        expect_error("unknown seam stage", normalize(k43499, "select_stack", bad_stage),
                     PlanError::UnknownSeamStage);
        const std::array<QueueElement, 1> empty_stage = {seam_element("")};
        expect_error("missing seam stage", normalize(k43499, "select_stack", empty_stage),
                     PlanError::UnknownSeamStage);
        const std::array<QueueElement, 1> pre_route = {seam_element("pre_route")};
        expect_error("pre_route is not queue-legal", normalize(k43499, "select_stack", pre_route),
                     PlanError::SeamStageIllegal);
    }

    if (failures != 0) {
        std::fprintf(stderr, "step_plan_test: FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("step_plan_test: ok (%zu reason tokens, all declaration rules)\n",
                sizeof(kReasons) / sizeof(kReasons[0]));
    return 0;
}