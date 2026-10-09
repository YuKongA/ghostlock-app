#pragma once
/** @file profile/run_plan.hpp
 *  What one document asks this run to do: a chain, and the steps in order.
 *
 *  The document names its selection at the root -- backend, route, terminal --
 *  and, when it carries one, the steps it selected. Resolving those into a chain
 *  and an ordered list of steps is the last thing that happens before anything
 *  runs, and it is where a document that asks for something this build cannot do
 *  is refused by name rather than attempted.
 *
 *  There is no falling back. A document that carries no queue has not said which
 *  steps to run, and the table's default path is not a substitute for being told:
 *  the temporary fallback that stood here was removed once both ends' documents
 *  carried their queue, and what used to be a guess is now a refusal named
 *  plan-missing. The queue itself is still the carrier; when the script becomes
 *  the source of the steps, this is where that lands.
 */
#include <cstddef>
#include <string_view>

#include "contract/chain_catalog.hpp"
#include "contract/step_catalog.hpp"
#include "profile/glkv3_document.hpp"

namespace ghostlock::profile {
    /** How many steps one run may ask for. */
    inline constexpr std::size_t kMaxRunSteps = 16U;

    /** Why a document could not be resolved into a run. Names follow the table. */
    enum class PlanVerdict : unsigned char {
        Ok = 0,
        DocumentInvalid = 1,
        /** The document names no backend, route or terminal this build has. */
        SelectionMissing = 2,
        /** No chain matches the selection. */
        ChainUnknown = 3,
        /** A chain matches but this build does not wire it end to end. */
        ChainNotWired = 4,
        /** The queue, or an entry in it, cannot be read. */
        QueueUnusable = 5,
        /** The steps do not describe any declared step set. */
        StepSetUnknown = 6,
    };

    [[nodiscard]] constexpr const char *plan_verdict_name(PlanVerdict verdict) noexcept {
        switch (verdict) {
            case PlanVerdict::Ok: return "ok";
            case PlanVerdict::DocumentInvalid: return "document-invalid";
            case PlanVerdict::SelectionMissing: return "backend-not-available";
            case PlanVerdict::ChainUnknown: return "route-not-available";
            case PlanVerdict::ChainNotWired: return "op-not-available";
            case PlanVerdict::QueueUnusable: return "plan-missing";
            case PlanVerdict::StepSetUnknown: return "unknown-op";
        }
        return "unknown";
    }

    /** What the run will do: the chain, and the steps in the order they run. */
    struct RunPlan final {
        const contract::ChainSpec *chain = nullptr;
        contract::StepId steps[kMaxRunSteps] = {};
        std::size_t step_count = 0U;
    };

    struct RunPlanResult final {
        PlanVerdict verdict = PlanVerdict::DocumentInvalid;
        RunPlan plan{};
    };

    /** Resolve a parsed document into the run it asks for. */
    [[nodiscard]] RunPlanResult resolve_run_plan(const Document &document) noexcept;

    /** The declared step set whose order is exactly these steps, or nullptr. */
    [[nodiscard]] const contract::StepSetOrder *step_set_for_steps(const contract::StepId *steps,

                                                                   std::size_t count) noexcept;
}  // namespace ghostlock::profile
