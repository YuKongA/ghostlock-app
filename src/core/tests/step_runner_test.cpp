/** @file tests/step_runner_test.cpp
 *  The runner against an action that records what it was asked to do.
 *
 *  Three cases: a chain that works runs every step in the catalogue's order and reports the
 *  step it ended on as Unknown; a chain that fails at the second step stops there, runs exactly
 *  two and names the one that failed; and an empty set is refused rather than called a success.
 */
#include <cstddef>
#include <cstdio>

#include "backend/cve_2026_43499/steps/step_runner.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::steps::run_step_set;
    using ghostlock::contract::kStepSetCount;
    using ghostlock::contract::StepId;
    using ghostlock::contract::StepSetOrder;
    using ghostlock::contract::step_set_order;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** An action that never fails and remembers the order it was called in. */
    struct Recording final {
        StepId seen[4] = {StepId::Unknown, StepId::Unknown, StepId::Unknown, StepId::Unknown};
        std::size_t calls = 0U;
        StepId fail_at = StepId::Unknown;
        bool run(StepId id) noexcept {
            if (calls < 4U) {
                seen[calls] = id;
            }
            ++calls;
            return id != fail_at;
        }
    };

    void a_chain_that_works_runs_everything_in_order() {
        const StepSetOrder *order = step_set_order(std::string_view("selinux_cred_seccomp"));
        expect(order != nullptr, "the full chain is in the catalogue");
        if (order == nullptr) {
            return;
        }
        Recording action;
        const auto run = run_step_set(*order, action);
        expect(run.ok && run.ran == order->count, "every step of the set ran and the run is ok");
        expect(run.stopped_at == StepId::Unknown, "and no step is named as the one that stopped it");
        expect(action.calls == order->count, "the action was called once per step");
        expect(action.seen[0U] == StepId::DisenforceSelinux &&
                   action.seen[1U] == StepId::CredentialForgery &&
                   action.seen[2U] == StepId::SeccompBypass,
               "in the order the catalogue declares, which is what the profile selects");
    }

    void a_chain_stops_where_it_fails() {
        const StepSetOrder *order = step_set_order(std::string_view("selinux_cred_seccomp"));
        expect(order != nullptr, "the full chain is in the catalogue");
        if (order == nullptr) {
            return;
        }
        Recording action;
        action.fail_at = StepId::CredentialForgery;
        const auto run = run_step_set(*order, action);
        expect(!run.ok, "a failing step leaves the run not ok");
        expect(run.ran == 2U, "and stops after it: two steps ran, not three");
        expect(run.stopped_at == StepId::CredentialForgery, "and it names the step that failed");
        const StepSetOrder empty{"empty", order->steps, 0U};
        Recording untouched;
        const auto none = run_step_set(empty, untouched);
        expect(!none.ok && none.ran == 0U, "a set with no steps is refused, not called a success");
        expect(untouched.calls == 0U, "and nothing was asked of the action");
        expect(kStepSetCount == 3U, "the catalogue exposes its three orders as data");
    }
}  // namespace

int main() {
    std::printf("step runner: the order it walks\n");
    a_chain_that_works_runs_everything_in_order();
    std::printf("step runner: where it stops\n");
    a_chain_stops_where_it_fails();
    if (g_failures != 0U) {
        std::printf("step runner: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("step runner: PASS (walks the declared order, stops at the first failure)\n");
    return 0;
}
