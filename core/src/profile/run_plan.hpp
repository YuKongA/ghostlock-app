#pragma once
/** @file profile/run_plan.hpp
 *  What one document asks this run to do: a chain, and the plan it will run.
 *
 *  The document declares ONE selection: a combination token, the identity the
 *  combination catalogue uses and the App shows. Everything else about the chain --
 *  backend, route, path, step set, terminal -- is derived from that token through
 *  the chain table, so a document and this build cannot disagree about which chain
 *  is meant, and there is no second place for a selection to live.
 *
 *  The steps are not declared by the document at all. The plan is a script, and
 *  the script is its ordering authority: a document carries one and this resolver
 *  hands it over; a document that carries none is refused as plan-missing. The
 *  queue that used to carry a step list is retired -- the rule is that it is
 *  refused on sight rather than ignored, because silently ignoring it would run a
 *  plan nobody asked for.
 */
#include <cstddef>
#include <string_view>

#include "contract/chain_catalog.hpp"
#include "profile/glkv3_document.hpp"
#include "profile/plan_delivery.hpp"

namespace ghostlock::profile {
    /** How many steps one run may ask for. */
    inline constexpr std::size_t kMaxRunSteps = 16U;

    /** Why a document could not be resolved into a run. Names follow the table. */
    enum class RunPlanVerdict : unsigned char {
        Ok = 0,
        DocumentInvalid = 1,
        /** The document declares no combination, or one this build does not have. */
        SelectionMissing = 2,
        /** The selection names no chain. */
        ChainUnknown = 3,
        /** A chain matches but this build does not wire it end to end. */
        ChainNotWired = 4,
        /** The document still carries the retired queue. */
        QueueRemoved = 5,
        /** The document carries no plan: nothing to run. */
        PlanMissing = 6,
        /** The plan is larger than a script may be. */
        PlanTooLarge = 7,
    };

    [[nodiscard]] constexpr const char *run_plan_verdict_name(RunPlanVerdict verdict) noexcept {
        switch (verdict) {
            case RunPlanVerdict::Ok: return "ok";
            case RunPlanVerdict::DocumentInvalid: return "document-invalid";
            case RunPlanVerdict::SelectionMissing: return "backend-not-available";
            case RunPlanVerdict::ChainUnknown: return "route-not-available";
            case RunPlanVerdict::ChainNotWired: return "op-not-available";
            case RunPlanVerdict::QueueRemoved: return "queue-removed";
            case RunPlanVerdict::PlanMissing: return "plan-missing";
            case RunPlanVerdict::PlanTooLarge: return "budget-exceeded";
        }
        return "unknown";
    }

    /** What the run will do: the chain, and the plan script it will load. */
    struct RunPlan final {
        const contract::ChainSpec *chain = nullptr;
        /** The delivered script: a view into the document's buffer. */
        PlanDelivery plan{};
    };

    struct RunPlanResult final {
        RunPlanVerdict verdict = RunPlanVerdict::DocumentInvalid;
        RunPlan plan{};
    };

    /** Resolve a parsed document into the run it asks for. */
    [[nodiscard]] RunPlanResult resolve_run_plan(const Document &document) noexcept;
}  // namespace ghostlock::profile
