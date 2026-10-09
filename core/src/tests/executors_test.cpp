/** @file tests/executors_test.cpp
 *  Each executor asks for its own step set, and stops where the action stops.
 *
 *  The sets are not written out here. They are read from the same contract arrays the runner uses,
 *  so a reorder in the catalogue cannot pass this test by being copied into it: the question asked
 *  is whether the executor runs the set its name claims, not whether two hand-written lists agree.
 */
#include <cstddef>
#include <cstdio>

#include "contract/step_catalog.hpp"
#include "pipeline/executors.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::steps::StepRun;
    using ghostlock::contract::kStepSetOrders;
    using ghostlock::contract::step_set_order;
    using ghostlock::contract::StepId;
    using ghostlock::contract::StepSetOrder;
    using ghostlock::pipeline::Cve43284PageCache_UmhForward;
    using ghostlock::pipeline::Cve43499W1W2_RootChild;
    using ghostlock::pipeline::Cve43499W1W3_RootChild;
    using ghostlock::pipeline::PathRun;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Records what it was asked, in order, and can be told to fail at one id. */
    struct RecordingAction final {
        StepId fail_at = StepId::Unknown;
        std::size_t calls = 0U;
        StepId seen[8U] = {};
        bool run(StepId id) noexcept {
            if (calls < 8U) {
                seen[calls] = id;
            }
            ++calls;
            return id != fail_at;
        }
    };

    /** The steps an executor should have asked for: read from the contract, not written here. */
    bool asked_for_its_own_set(const RecordingAction &action, const char *set) {
        const StepSetOrder *order = step_set_order(set);
        if (order == nullptr || action.calls != order->count) {
            return false;
        }
        for (std::size_t i = 0U; i < order->count; ++i) {
            if (action.seen[i] != order->steps[i]) {
                return false;
            }
        }
        return true;
    }

    void each_executor_runs_the_set_its_name_claims() {
        RecordingAction w1w3;
        Cve43499W1W3_RootChild<RecordingAction> longer(w1w3);
        const PathRun a = longer.run();
        expect(a.ok, "the longer chain completes when nothing fails");
        expect(asked_for_its_own_set(w1w3, "selinux_cred_seccomp"),
               "and it asked for exactly the seccomp step set, in the contract's order");

        RecordingAction w1w2;
        Cve43499W1W2_RootChild<RecordingAction> shorter(w1w2);
        const PathRun b = shorter.run();
        expect(b.ok, "the shorter chain completes too");
        expect(asked_for_its_own_set(w1w2, "selinux_cred"),
               "and it asked for the set without seccomp");
        expect(w1w2.calls + 1U == w1w3.calls,
               "the shorter chain asks for exactly one step fewer, which is the seccomp step");

        RecordingAction cache;
        Cve43284PageCache_UmhForward<RecordingAction> page(cache);
        const PathRun c = page.run();
        expect(c.ok, "the page cache path completes");
        expect(asked_for_its_own_set(cache, "pagecache"), "and asks for the single step set");
        expect(cache.calls == 1U, "which is one step");
    }

    void a_failing_step_stops_the_path_and_is_named() {
        const StepSetOrder *order = step_set_order("selinux_cred_seccomp");
        expect(order != nullptr && order->count >= 2U, "the seccomp set has at least two steps");
        RecordingAction action;
        action.fail_at = order->steps[1U];
        Cve43499W1W3_RootChild<RecordingAction> longer(action);
        const PathRun run = longer.run();
        expect(!run.ok, "a failing step makes the path a failure");
        expect(run.steps.stopped_at == order->steps[1U], "and the stop is named");
        expect(action.calls == 2U, "with the step after it never asked for");
        expect(run.steps.ran == 2U, "and two steps reported as run, the failing one included");
    }

    void the_three_targets_are_distinct_and_match_their_names() {
        expect(Cve43499W1W3_RootChild<RecordingAction>::kBackend == "cve_2026_43499",
               "the longer chain is the first backend");
        expect(Cve43499W1W2_RootChild<RecordingAction>::kStepSet == "selinux_cred",
               "the shorter chain is the shorter set");
        expect(Cve43284PageCache_UmhForward<RecordingAction>::kBackend == "cve_2026_43284",
               "the page cache path is the second backend");
        std::printf("executors: %zu step sets in the catalogue\n",
                    sizeof(kStepSetOrders) / sizeof(kStepSetOrders[0]));
    }
}  // namespace

int main() {
    std::printf("executors: each runs its own set\n");
    each_executor_runs_the_set_its_name_claims();
    std::printf("executors: a failing step\n");
    a_failing_step_stops_the_path_and_is_named();
    std::printf("executors: the names\n");
    const bool names = true;
    (void)names;
    the_three_targets_are_distinct_and_match_their_names();
    if (g_failures != 0U) {
        std::printf("executors: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("executors: PASS (three paths, each running the set its name claims)\n");
    return 0;
}
