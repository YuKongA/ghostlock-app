/** @file tests/step_action_test.cpp
 *  The step-to-request decision, and the runner that walks a set with an action.
 *
 *  Two layers are checked. The decision is pure, so it is pinned value by value, including the two
 *  cases that must be refused: a step this backend does not run, and a step whose target the run did
 *  not learn. The runner is checked with an action that records what it was asked, so ordering,
 *  stopping and the empty set are visible without a device.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/steps/action.hpp"
#include "backend/cve_2026_43499/steps/step_runner.hpp"
#include "backend/cve_2026_43499/memory/write_request.hpp"
#include "contract/step_catalog.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::check_request;
    using ghostlock::backend::cve_2026_43499::memory::RequestVerdict;
    using ghostlock::backend::cve_2026_43499::memory::WriteMode;
    using ghostlock::backend::cve_2026_43499::memory::WriteRequest;
    using ghostlock::backend::cve_2026_43499::steps::request_for;
    using ghostlock::backend::cve_2026_43499::steps::run_step_set;
    using ghostlock::backend::cve_2026_43499::steps::StepRun;
    using ghostlock::backend::cve_2026_43499::steps::StepTargets;
    using ghostlock::contract::StepId;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    StepTargets sample() {
        StepTargets t{};
        t.selinux_enforcing = 0x1000U;
        t.credential = 0x2000U;
        t.seccomp_mode = 0x3000U;
        return t;
    }

    void each_step_takes_its_own_field_and_mode() {
        const StepTargets t = sample();
        const WriteRequest selinux = request_for(StepId::DisenforceSelinux, t);
        expect(selinux.target == 0x1000U, "selinux step targets the enforcing field");
        expect(selinux.mode == WriteMode::Zero, "and clears it rather than replacing it");
        expect(check_request(selinux) == RequestVerdict::Ok, "so the request passes its own check");

        const WriteRequest seccomp = request_for(StepId::SeccompBypass, t);
        expect(seccomp.target == 0x3000U, "the seccomp step targets the mode field");
        expect(seccomp.mode == WriteMode::Zero, "and clears it the same way");
        expect(check_request(seccomp) == RequestVerdict::Ok, "which also checks out");

        const WriteRequest cred = request_for(StepId::CredentialForgery, t);
        expect(cred.target == 0x2000U, "the credential step targets the fragment");
        expect(cred.mode == WriteMode::Credential, "and writes values rather than clearing");
        expect(check_request(cred) == RequestVerdict::Ok, "which checks out too");
    }

    void a_step_this_backend_does_not_run_is_refused_by_name() {
        const WriteRequest page = request_for(StepId::PageCacheWrite, sample());
        expect(page.mode == WriteMode::Disabled,
               "the page cache write is not this backend's step, so it asks for nothing");
        /* Which refusal comes out is decided by the order the check reads its fields, and it reads
         *  the target first. An unasked-for step has neither field, so the answer is NoTarget; the
         *  NoMode branch is reached only when an address was learned and the mode was not. Both are
         *  asserted below, because pinning only one would leave the other unguarded. */
        expect(check_request(page) == RequestVerdict::NoTarget,
               "and is refused by name, not silently");
        const WriteRequest unknown = request_for(StepId::Unknown, sample());
        expect(check_request(unknown) == RequestVerdict::NoTarget, "as is the unknown step");

        WriteRequest with_target = request_for(StepId::DisenforceSelinux, sample());
        with_target.mode = WriteMode::Disabled;
        expect(check_request(with_target) == RequestVerdict::NoMode,
               "and an address with no mode is refused as having no mode, the other refusal");
    }

    void a_target_the_run_did_not_learn_is_refused_by_name() {
        StepTargets empty{};
        const WriteRequest selinux = request_for(StepId::DisenforceSelinux, empty);
        expect(selinux.mode == WriteMode::Zero, "the mode is still chosen");
        expect(check_request(selinux) == RequestVerdict::NoTarget,
               "but with no address learned the request is refused as having no target");
    }

    /** An action that records the ids it was asked for and can be told to fail at one. */
    class RecordingAction final {
      public:
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

    void the_runner_walks_in_order_and_stops_at_the_first_failure() {
        using ghostlock::contract::kStepSetOrders;
        RecordingAction action;
        const StepRun all = run_step_set(kStepSetOrders[0U], action);
        expect(action.calls == kStepSetOrders[0U].count,
               "the runner asked for every step of the set when none failed");
        expect(all.ok && all.ran == kStepSetOrders[0U].count, "and reported the whole set as run");
        expect(all.stopped_at == StepId::Unknown, "with no stop recorded");

        RecordingAction stopper;
        stopper.fail_at = kStepSetOrders[0U].steps[0U];
        const StepRun stopped = run_step_set(kStepSetOrders[0U], stopper);
        expect(stopper.calls == 1U, "a failing first step means the second was never asked for");
        expect(!stopped.ok, "and the run is not ok");
        expect(stopped.stopped_at == kStepSetOrders[0U].steps[0U], "and names where it stopped");
    }

    void an_empty_set_is_refused_rather_than_reported_as_success() {
        RecordingAction action;
        const ghostlock::contract::StepSetOrder empty{"empty", nullptr, 0U};
        const StepRun run = run_step_set(empty, action);
        expect(!run.ok, "a set that ran nothing is not a set that worked");
        expect(action.calls == 0U, "and nothing was asked of the action");
    }
}  // namespace

int main() {
    std::printf("step action: what each step asks for\n");
    each_step_takes_its_own_field_and_mode();
    std::printf("step action: refusals\n");
    a_step_this_backend_does_not_run_is_refused_by_name();
    a_target_the_run_did_not_learn_is_refused_by_name();
    std::printf("step action: the runner\n");
    the_runner_walks_in_order_and_stops_at_the_first_failure();
    an_empty_set_is_refused_rather_than_reported_as_success();
    if (g_failures != 0U) {
        std::printf("step action: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("step action: PASS (maps three steps, refuses the rest by name, walks in order)\n");
    return 0;
}
