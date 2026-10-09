#pragma once
/** @file backend/cve_2026_43499/steps/step_runner.hpp
 *  Running a step set, in the order the contract gives it, stopping at the first failure.
 *
 *  A step set is an ordered list of steps and the contract already holds them: the three steps,
 *  the two chains and the single-step page cache order are data in contract/step_catalog.hpp,
 *  not a sequence written in a function. What was missing is the thing that walks one of those
 *  orders and calls the step. That is all this is: it does not decide which set to run, it does
 *  not know what a step does, and it does not retry. The set is chosen by the plan, the work is
 *  the action's, and a failure stops the chain and is reported with the step it happened at.

 *  ADR-0011 makes the Lua the authority on execution order, so the order handed in here is
 *  expected to come from there; the catalogue table is the default the Lua is generated from,
 *  not the last word. That is why this takes an order rather than reaching for one.
 *
 *  Why a separate piece: the op registry names operations whose implementations do not exist
 *  yet, and the first thing any of them needs is this walk. Keeping it free of the session and
 *  the profile is what makes it testable now, with an action that records the calls it got.
 */
#include <concepts>
#include <cstddef>

#include "contract/step_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** What a step is, from the runner's side: given which step, say whether it worked. */
    template <class Action>
    concept StepAction = requires(Action &action, contract::StepId id) {
        { action.run(id) } -> std::convertible_to<bool>;
    };

    /** What one run did. */
    struct StepRun final {
        /** Whether every step in the set succeeded. */
        bool ok = false;
        /** How many steps ran, including the one that failed. */
        std::size_t ran = 0U;
        /** Which step the chain stopped at, or Unknown when it completed. */
        contract::StepId stopped_at = contract::StepId::Unknown;
    };

    /** Walk the set and run every step until one fails. An empty set is refused rather than
     *  reported as success: a chain that ran nothing and a chain that worked are not the same
     *  answer, which is the same rule the collision search holds its empty range to. */
    template <StepAction Action>
    [[nodiscard]] StepRun run_step_set(const contract::StepSetOrder &order, Action &action) noexcept {
        StepRun run{};
        if (order.step_set == nullptr || order.steps == nullptr || order.count == 0U) {
            return run;
        }
        for (std::size_t i = 0U; i < order.count; ++i) {
            ++run.ran;
            if (!action.run(order.steps[i])) {
                run.stopped_at = order.steps[i];
                return run;
            }
        }
        run.ok = true;
        run.stopped_at = contract::StepId::Unknown;
        return run;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
