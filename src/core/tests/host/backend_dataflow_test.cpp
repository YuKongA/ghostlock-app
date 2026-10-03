/*
 * Host data-flow test: drive the real cve_2026_43499 backend and root_child
 * terminal through Pipeline::run with the attack primitives stubbed. The
 * stage sequence, retries, chain rounds and handoff all execute unchanged; the
 * script decides the verify/probe outcomes.
 */

#include "host_attack_script.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "session/core_session.hpp"
#include "terminal/root_child.hpp"

#include <cstdio>
#include <cstdint>

namespace {
    using ghostlock::host::script;

    void reset_script() {
        auto &s = script();
        s = ghostlock::host::HostAttackScript{};
        s.spawn_default = {4242, 0xffff888000001000ULL};
    }

    int32_t count_calls(const char *name) {
        int32_t n = 0;
        for (const auto &call : script().calls) {
            if (call == name) ++n;
        }
        return n;
    }

    bool expect(bool condition, const char *message) {
        if (!condition) std::printf("FAIL: %s\n", message);
        return condition;
    }

    ghostlock::pipeline::RunResult run_once() {
        ghostlock::profile::kernel_offsets decoded{};
        decoded.meta.kernel_major = 6;
        decoded.execution.w1_attempts = 3;
        decoded.execution.w1_scratch_repair_attempts = 1;
        decoded.execution.w2_attempts = 4;
        decoded.execution.w3_chain_rounds = 2;
        decoded.route = ghostlock::profile::kRouteSelectStack;
        decoded.execution.w3_attempts = 3;
        ghostlock::backend::cve43499_state_construct(
                ghostlock::session::g_exploit_session);
        ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).profile =
                ghostlock::profile::TargetProfile::from(&decoded);
        return ghostlock::pipeline::Pipeline<
            ghostlock::backend::Cve2026_43499Policy,
            ghostlock::terminal::RootChildPolicy>::run(
            ghostlock::session::g_exploit_session, decoded, nullptr, true);
    }

    bool test_happy_path() {
        reset_script();
        const auto result = run_once();
        bool ok = true;
        ok &= expect(result.code == ghostlock::pipeline::RunCode::Completed,
                     "happy: pipeline should complete");
        ok &= expect(result.stage == ghostlock::pipeline::RunStage::Terminal,
                     "happy: terminal stage is the terminal");
        ok &= expect(count_calls("spawn") == 1, "happy: exactly one victim spawn");
        ok &= expect(count_calls("verify_w2") >= 1, "happy: W2 verified");
        ok &= expect(count_calls("verify_seccomp") >= 1, "happy: W3 seccomp verified");
        ok &= expect(count_calls("route") >= 1, "happy: at least one route write");
        return ok;
    }

    bool test_w2_retry_until_success() {
        reset_script();
        script().verify_w2 = {0, 0, 1};
        const auto result = run_once();
        bool ok = true;
        ok &= expect(result.code == ghostlock::pipeline::RunCode::Completed,
                     "retry: pipeline completes after late W2 success");
        /* First attempt verifies once; the second attempt verifies before and
         * after the write, where the queued success lands. */
        ok &= expect(count_calls("verify_w2") == 3, "retry: verify_w2 called 3 times");
        ok &= expect(count_calls("spawn") == 1, "retry: same child retried, not respawned");
        return ok;
    }

    bool test_w2_exhausts_attempts() {
        reset_script();
        script().verify_w2_default = 0;
        const auto result = run_once();
        const uint32_t attempts =
                ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).profile.w2_attempts();
        bool ok = true;
        ok &= expect(result.code == ghostlock::pipeline::RunCode::Failed,
                     "exhaust: pipeline fails");
        ok &= expect(result.stage == ghostlock::pipeline::RunStage::Backend,
                     "exhaust: failure is in the backend");
        ok &= expect(count_calls("verify_w2") >= static_cast<int32_t>(attempts),
                     "exhaust: verify_w2 ran at least w2_attempts times");
        return ok;
    }

    bool test_selinux_retry_then_w2() {
        reset_script();
        script().verify_selinux = {0, 1};
        const auto result = run_once();
        bool ok = true;
        ok &= expect(result.code == ghostlock::pipeline::RunCode::Completed,
                     "selinux: pipeline completes");
        ok &= expect(count_calls("verify_selinux") == 2,
                     "selinux: verify_selinux retried twice");
        return ok;
    }
} // namespace

int main() {
    bool ok = true;
    ok &= test_happy_path();
    ok &= test_w2_retry_until_success();
    ok &= test_w2_exhausts_attempts();
    ok &= test_selinux_retry_then_w2();
    if (ok) {
        std::puts("backend_dataflow_test: ok");
        return 0;
    }
    return 1;
}
