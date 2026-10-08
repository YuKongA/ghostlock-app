#ifndef GHOSTLOCK_CONTRACT_STEP_PLAN_HPP
#define GHOSTLOCK_CONTRACT_STEP_PLAN_HPP

/* Canonical step-plan normalization (step-queue design doc sections 4.1-4.3,
 * 5-Q1/Q2/Q3; batch M1.1).
 *
 * Pure function over a NEUTRAL in-memory queue: the HOCON/wire parser of a later
 * batch builds `QueueInput` (no wire or Document dependency here) and calls
 * `normalize_step_queue`, which applies every declaration-time rule and returns
 * either a `CanonicalPlan` or a named `PlanError`. `plan_error_reason` spells the
 * exact `reason=` token the runtime diagnostics use (design doc section 10.1).
 *
 * M1.1 boundary: nothing calls this yet (no production wiring, no behaviour
 * change). The verdict split implements design doc section 4.3:
 *   - the normalized (backend, route, steps) tuple equals a device-verified
 *     preset in kCombinationCatalog  => supported;
 *   - otherwise the queue must DECLARE experimental (U5 rule 2: the declaration
 *     is a request, the verdict is computed -- a declared queue that matches a
 *     verified preset is still supported);
 *   - otherwise the plan is refused (experimental-not-declared).
 *
 * Layering: contract/ only (host-compilable, no backend/pipeline includes). The
 * seam stage vocabulary is the contract-side authority in
 * contract/countermeasure.hpp (stage_token), so no second stage table exists. */

#include "contract/countermeasure.hpp"
#include "contract/identity.hpp"
#include "contract/model.hpp"
#include "contract/step_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ghostlock::contract {
    /* Bounded canonical plan: the queue is a static declaration, so a fixed
     * capacity keeps the normalizer allocation-free and reuses the same bounded
     * style as the frozen payload design (ko count <= 8). */
    inline constexpr std::size_t kMaxQueueSteps = 8U;
    inline constexpr std::size_t kMaxQueueSeams = 4U;

    enum class PlanVerdict : std::uint8_t {
        None = 0,         /* not computed (the result carries an error) */
        Supported = 1,    /* equals a device-verified preset */
        Experimental = 2, /* legal, declared experimental, not a verified preset */
    };

    enum class PlanError : std::uint8_t {
        None = 0,
        EmptyQueue,
        NoSteps,
        TooManySteps,
        TooManySeams,
        ElementNotObject,
        ElementUnknownKey,
        ParamsReserved,
        StepAndSeam,
        MissingStepOrSeam,
        StepRouteNotAllowed,
        UnknownStepToken,
        CrossBackendStep,
        DuplicateStep,
        MissingDependency,
        NotACanonicalPrefix, /* the canonical-prefix rule also pins the order */
        RouteRequired,
        RouteNotApplicable,
        RouteDuplicated,
        UnknownRouteToken,
        UnknownSeamType,
        UnknownSeamStage,
        SeamStageIllegal,
        ExperimentalNotDeclared,
    };

    /* The exact `reason=` token of the design doc diagnostics (section 10.1);
     * pinned by step_plan_test so a rename cannot silently change the contract. */
    [[nodiscard]] constexpr std::string_view plan_error_reason(PlanError error) noexcept {
        switch (error) {
            case PlanError::None: return "none";
            case PlanError::EmptyQueue: return "empty-queue";
            case PlanError::NoSteps: return "no-steps";
            case PlanError::TooManySteps: return "too-many-steps";
            case PlanError::TooManySeams: return "too-many-seams";
            case PlanError::ElementNotObject: return "queue-element-not-object";
            case PlanError::ElementUnknownKey: return "queue-element-unknown-key";
            case PlanError::ParamsReserved:
                return "params-reserved-for-future-step-parameters";
            case PlanError::StepAndSeam: return "queue-element-step-and-seam";
            case PlanError::MissingStepOrSeam: return "queue-element-missing-step-or-seam";
            case PlanError::StepRouteNotAllowed: return "step-route-not-allowed";
            case PlanError::UnknownStepToken: return "unknown-step-token";
            case PlanError::CrossBackendStep: return "cross-backend-step";
            case PlanError::DuplicateStep: return "duplicate-step";
            case PlanError::MissingDependency: return "missing-dependency";
            case PlanError::NotACanonicalPrefix: return "not-a-canonical-prefix";
            case PlanError::RouteRequired: return "route-required";
            case PlanError::RouteNotApplicable: return "route-not-applicable";
            case PlanError::RouteDuplicated: return "route-duplicated";
            case PlanError::UnknownRouteToken: return "unknown-route-token";
            case PlanError::UnknownSeamType: return "unknown-seam-type";
            case PlanError::UnknownSeamStage: return "unknown-seam-stage";
            case PlanError::SeamStageIllegal: return "seam-stage-illegal";
            case PlanError::ExperimentalNotDeclared: return "experimental-not-declared";
        }
        return "unknown";
    }

    /* One neutral queue element: what a parser can know without naming the wire
     * or the HOCON shape. `is_object == false` is the rejected bare-string form
     * (design doc U9). */
    struct QueueElement final {
        bool is_object = true;
        std::string_view step{};        /* step token of a step element */
        std::string_view seam{};        /* seam type token of a seam element */
        std::string_view stage{};       /* seam stage token (required for a seam) */
        bool has_step_route = false;    /* a route written on the element (forbidden) */
        bool has_params = false;        /* the reserved params key is present */
        std::string_view unknown_key{}; /* first key outside the known set */
    };

    struct QueueInput final {
        BackendKind backend = BackendKind::Cve2026_43499;
        std::string_view route{};      /* queue-level route token ("" = absent) */
        bool route_duplicated = false; /* the source declared route more than once */
        bool experimental_declared = false;
        std::span<const QueueElement> elements{};
    };

    struct SeamSlot final {
        CountermeasureStage stage = CountermeasureStage::PreSpawn;
        std::uint8_t position = 0U; /* index in the queue where the seam sits */
    };

    struct CanonicalPlan final {
        BackendKind backend = BackendKind::Cve2026_43499;
        profile::RouteKind route = profile::RouteKind::None;
        std::array<StepId, kMaxQueueSteps> steps{};
        std::size_t step_count = 0U;
        std::array<SeamSlot, kMaxQueueSeams> seams{};
        std::size_t seam_count = 0U;
        PlanVerdict verdict = PlanVerdict::None;
        CombinationKind preset = CombinationKind::Unknown; /* matched verified preset */
    };

    struct PlanResult final {
        PlanError error = PlanError::None;
        std::size_t error_index = 0U; /* element index for element-scoped errors */
        CanonicalPlan plan{};
        [[nodiscard]] constexpr bool ok() const noexcept {
            return error == PlanError::None;
        }
    };

    namespace step_plan_detail {
        /* The frozen plugin seam is the only seam type today; the identifier is
         * the reserved type name from the plugin design (section 5-Q3). */
        [[nodiscard]] constexpr bool known_seam_type(std::string_view seam) noexcept {
            return seam == "plugin";
        }

        /* R1 position rule: a seam may sit before the steps (pre_spawn) or after
         * them (post_terminal); inside [w1..w3] the plugin .so must not be mapped
         * while the PI waiter is alive, so post_spawn/pre_terminal/pre_route are
         * refused there. */
        [[nodiscard]] constexpr bool stage_allowed_at(bool first, bool last,
                                                      CountermeasureStage stage) noexcept {
            if (first) return stage == CountermeasureStage::PreSpawn;
            if (last) return stage == CountermeasureStage::PostTerminal;
            return false;
        }

        [[nodiscard]] constexpr bool stage_known(CountermeasureStage &out,
                                                 std::string_view token) noexcept {
            for (const CountermeasureStage candidate :
                 {CountermeasureStage::PreSpawn, CountermeasureStage::PostSpawn,
                  CountermeasureStage::PreTerminal, CountermeasureStage::PreRoute,
                  CountermeasureStage::PostTerminal}) {
                if (stage_token(candidate) == token) {
                    out = candidate;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] constexpr std::size_t plan_index_of(const CanonicalPlan &plan,
                                                          StepId id) noexcept {
            for (std::size_t i = 0; i < plan.step_count; ++i) {
                if (plan.steps[i] == id) return i;
            }
            return kMaxQueueSteps; /* not present */
        }

        [[nodiscard]] constexpr bool routed_backend(BackendKind backend) noexcept {
            return backend == BackendKind::Cve2026_43499;
        }

        /* Verified preset: an available catalogue row on the same backend and
         * route whose alias steps equal the plan steps, in order. */
        [[nodiscard]] constexpr CombinationKind match_verified_preset(
                const CanonicalPlan &plan) noexcept {
            for (const CombinationSpec &spec : kCombinationCatalog) {
                if (!spec.available || spec.backend != plan.backend) continue;
                if (spec.route != plan.route) continue;
                const StepSetAlias *alias = step_set_alias(spec.steps);
                if (alias == nullptr || alias->steps.size() != plan.step_count) continue;
                bool same = true;
                for (std::size_t i = 0; i < plan.step_count; ++i) {
                    if (alias->steps[i] != plan.steps[i]) {
                        same = false;
                        break;
                    }
                }
                if (same) return spec.kind;
            }
            return CombinationKind::Unknown;
        }
    } // namespace step_plan_detail

    [[nodiscard]] inline PlanResult normalize_step_queue(const QueueInput &input) noexcept {
        PlanResult result;
        CanonicalPlan &plan = result.plan;
        plan.backend = input.backend;

        /* ---- queue-level route (design doc 5-Q2) ---- */
        if (input.route_duplicated) {
            result.error = PlanError::RouteDuplicated;
            return result;
        }
        if (step_plan_detail::routed_backend(input.backend)) {
            if (input.route.empty()) {
                result.error = PlanError::RouteRequired;
                return result;
            }
            const std::uint8_t wire = profile::route_kind_from_string(input.route);
            if (wire == profile::kRouteNone) {
                result.error = PlanError::UnknownRouteToken;
                return result;
            }
            plan.route = static_cast<profile::RouteKind>(wire);
        } else if (!input.route.empty()) {
            result.error = PlanError::RouteNotApplicable;
            return result;
        }

        /* ---- elements ---- */
        if (input.elements.empty()) {
            result.error = PlanError::EmptyQueue;
            return result;
        }
        for (std::size_t index = 0; index < input.elements.size(); ++index) {
            const QueueElement &element = input.elements[index];
            result.error_index = index;
            if (!element.is_object) {
                result.error = PlanError::ElementNotObject;
                return result;
            }
            if (!element.unknown_key.empty()) {
                result.error = PlanError::ElementUnknownKey;
                return result;
            }
            if (element.has_params) {
                result.error = PlanError::ParamsReserved;
                return result;
            }
            if (element.has_step_route) {
                result.error = PlanError::StepRouteNotAllowed;
                return result;
            }
            const bool has_step = !element.step.empty();
            const bool has_seam = !element.seam.empty();
            if (has_step && has_seam) {
                result.error = PlanError::StepAndSeam;
                return result;
            }
            if (!has_step && !has_seam) {
                result.error = PlanError::MissingStepOrSeam;
                return result;
            }
            if (has_step) {
                if (!element.stage.empty()) {
                    result.error = PlanError::ElementUnknownKey;
                    return result;
                }
                const StepSpec *spec = step_spec_from_token(element.step, input.backend);
                if (spec == nullptr) {
                    for (const StepSpec &other : kStepCatalog) {
                        if (other.token == element.step) {
                            result.error = PlanError::CrossBackendStep;
                            return result;
                        }
                    }
                    result.error = PlanError::UnknownStepToken;
                    return result;
                }
                if (plan.step_count >= kMaxQueueSteps) {
                    result.error = PlanError::TooManySteps;
                    return result;
                }
                if (step_plan_detail::plan_index_of(plan, spec->id) != kMaxQueueSteps) {
                    result.error = PlanError::DuplicateStep;
                    return result;
                }
                plan.steps[plan.step_count] = spec->id;
                ++plan.step_count;
                continue;
            }
            if (!step_plan_detail::known_seam_type(element.seam)) {
                result.error = PlanError::UnknownSeamType;
                return result;
            }
            CountermeasureStage stage = CountermeasureStage::PreSpawn;
            if (!step_plan_detail::stage_known(stage, element.stage)) {
                result.error = PlanError::UnknownSeamStage;
                return result;
            }
            const bool first = index == 0U;
            const bool last = index + 1U == input.elements.size();
            if (!step_plan_detail::stage_allowed_at(first, last, stage)) {
                result.error = PlanError::SeamStageIllegal;
                return result;
            }
            if (plan.seam_count >= kMaxQueueSeams) {
                result.error = PlanError::TooManySeams;
                return result;
            }
            plan.seams[plan.seam_count] =
                    SeamSlot{stage, static_cast<std::uint8_t>(index)};
            ++plan.seam_count;
        }
        if (plan.step_count == 0U) {
            result.error = PlanError::NoSteps;
            return result;
        }

        /* ---- dependencies, order and the canonical prefix (U4) ---- */
        for (std::size_t i = 0; i < plan.step_count; ++i) {
            const StepSpec *spec = step_spec(plan.steps[i]);
            result.error_index = i;
            for (std::uint8_t bit = 0; bit < 8U; ++bit) {
                const std::uint8_t mask = static_cast<std::uint8_t>(1U << bit);
                if ((spec->deps_mask & mask) == 0U) continue;
                const StepSpec *dependency = step_at_slot(spec->backend, bit);
                if (dependency == nullptr ||
                    step_plan_detail::plan_index_of(plan, dependency->id) == kMaxQueueSteps) {
                    result.error = PlanError::MissingDependency;
                    return result;
                }
            }
            /* The canonical-prefix rule (U4) already pins the order, so there is
             * no separate order error: a non-monotonic sequence cannot satisfy
             * "element i is slot i". */
            if (spec->slot != i) {
                result.error = PlanError::NotACanonicalPrefix;
                return result;
            }
        }

        /* ---- verdict (U5 rule 2: the declaration is a request) ---- */
        plan.preset = step_plan_detail::match_verified_preset(plan);
        if (plan.preset != CombinationKind::Unknown) {
            plan.verdict = PlanVerdict::Supported;
            return result;
        }
        if (!input.experimental_declared) {
            result.error = PlanError::ExperimentalNotDeclared;
            return result;
        }
        plan.verdict = PlanVerdict::Experimental;
        return result;
    }
} // namespace ghostlock::contract

#endif