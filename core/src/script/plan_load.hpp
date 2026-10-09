#pragma once
/** @file script/plan_load.hpp
 *  Loading the plan: running the delivered script once, in a fresh sandbox.
 *
 *  This is a composition and not a mechanism. The sandbox owns assembly, budgets
 *  and the refusal names; the delivery reader owns which script is effective and
 *  whether there is one at all. What is left -- open, install the read-only
 *  snapshot, run, close -- is what this file does, in that order, once per run.
 *
 *  It adds no vocabulary: the outcome is the sandbox's own rejection, so a script
 *  with a syntax error and a script that overruns its budget are reported under
 *  the names the single table already carries rather than under new ones.
 *
 *  One script per run, and no second source: the caller decides which script is
 *  effective (a user script overrides a built-in, per the plan spec) and hands it
 *  here; there is no merging of two plans and no reading of a second ordering
 *  source anywhere in this path.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "script/call_sink.hpp"
#include "script/sandbox.hpp"

namespace ghostlock::script {
    /** What loading a plan needs: the injected budgets and where calls go. */
    struct PlanLoadInput final {
        ScriptBudget budget{};
        CallSink sink{};
        /** The read-only snapshot the script may read, if any. */
        const CfgField *cfg = nullptr;
        std::size_t cfg_count = 0U;
    };

    /** Run one delivered script. The outcome is the sandbox's own rejection, so it
     *  is the same name that would be reported for the same script anywhere else. */
    [[nodiscard]] ScriptRejection load_plan(std::string_view script, const PlanLoadInput &input) noexcept;
}  // namespace ghostlock::script
