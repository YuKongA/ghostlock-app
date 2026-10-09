/** @file script/plan_load.cpp
 *  One script, one sandbox, one run.
 */
#include "script/plan_load.hpp"

namespace ghostlock::script {
    ScriptRejection load_plan(std::string_view script, const PlanLoadInput &input) noexcept {
        /* An empty script is not a plan: the delivery reader refuses it before it
         *  gets here, and this is the second line of defence for a caller that did
         *  not ask. The name is the plan's, not the sandbox's, because what is
         *  missing is the plan rather than something wrong with a script. */
        if (script.data() == nullptr || script.size() == 0U) {
            return ScriptRejection::PlanSyntax;
        }
        Sandbox sandbox;
        if (!sandbox.open(input.budget, input.sink)) {
            /* The sandbox could not be opened at all: nothing about this script is
             *  at fault, and the caller must not read it as a plan rejection. */
            return ScriptRejection::BudgetExceeded;
        }
        if (input.cfg != nullptr && input.cfg_count != 0U) {
            if (!sandbox.set_cfg_snapshot(input.cfg, input.cfg_count)) {
                sandbox.close();
                return ScriptRejection::CfgUnknownField;
            }
        }
        const ScriptRejection rejection = sandbox.run(script);
        sandbox.close();
        return rejection;
    }
}  // namespace ghostlock::script
