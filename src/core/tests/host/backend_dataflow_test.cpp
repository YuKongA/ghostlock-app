/*
 * Host data-flow test: drive the real cve_2026_43499 backend and root_child
 * terminal through Pipeline::run with the attack primitives stubbed. The
 * stage sequence, retries, chain rounds and handoff all execute unchanged; the
 * script decides the verify/probe outcomes.
 */

#include "host_attack_script.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include "backend/cve_2026_43499/capability_adapters.hpp"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "session/core_session.hpp"
#include "backend/cve_2026_43499/terminal/root_child.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <span>

namespace {
    using ghostlock::host::script;

    /* Pre-run profile snapshot: Pipeline::run's RAII guard destroys the backend
     * state at run exit, so the profile must be sampled before the run. */
    uint32_t last_w2_attempts = 0;

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

    template <class Backend>
    ghostlock::pipeline::RunResult run_once_with() {
        ghostlock::profile::Document document;
        document.release = "6.6.77-host-dataflow";
        document.terminal = static_cast<uint16_t>(
                ghostlock::contract::TerminalKind::RootChild);
        document.backend = static_cast<uint16_t>(
                ghostlock::contract::BackendKind::Cve2026_43499);
        document.middleware = ghostlock::profile::kRouteSelectStack;
        const auto add = [&document](std::string_view section,
                                     std::string_view key, uint64_t raw) {
            ghostlock::profile::Section *found = document.find_section(section);
            if (found == nullptr) found = &document.append_section(section);
            found->add(key, raw);
        };
        add("common", "kernel_major", 6);
        add("backend.cve_2026_43499.execution.stages", "w1_attempts", 3);
        add("backend.cve_2026_43499.execution.stages", "w1_scratch_repair_attempts", 1);
        add("backend.cve_2026_43499.execution.stages", "w2_attempts", 4);
        add("backend.cve_2026_43499.execution.stages", "w3_chain_rounds", 2);
        add("backend.cve_2026_43499.execution.stages", "w3_attempts", 3);
        last_w2_attempts = 4;
        /* Pipeline::run constructs the backend state, state_from binds the
         * Document, and its RAII guard destroys the state on exit. The install
         * hook is a host no-op (attack_stub.cpp). */
        return ghostlock::pipeline::Pipeline<
            Backend,
            ghostlock::backend::cve_2026_43499::terminal::RootChildPolicy>::run(
            ghostlock::session::g_exploit_session, document, nullptr, true);
    }

    ghostlock::pipeline::RunResult run_once() {
        return run_once_with<ghostlock::backend::Cve2026_43499Policy>();
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
        const uint32_t attempts = last_w2_attempts;
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

    /* Beta: the Tier 1 KernelMemory adapter the PreSpawn/PostSpawn blocks build
     * must report the unimplemented primitives as an explicit error, never a
     * fake 0 (R7), and its inherited lifecycle defaults stay fail-closed. */
    bool test_capability_adapters() {
        using ghostlock::backend::cve_2026_43499::Tier1KernelMemory;
        using ghostlock::backend::cve_2026_43499::route::SelectPolicy;
        using ghostlock::contract::CapabilityError;
        using ghostlock::contract::CapabilityState;
        using ghostlock::contract::CarrierKind;
        using ghostlock::contract::MemoryChannel;

        Tier1KernelMemory<SelectPolicy> kernel;
        std::byte buffer[8]{};
        bool ok = true;

        const auto read = kernel.read(
                0x1000, std::span<std::byte>(buffer), MemoryChannel::Unavailable);
        ok &= expect(!read.has_value(), "adapter: read returns an error");
        ok &= expect(read.error() == CapabilityError::Unsupported,
                     "adapter: read is Unsupported, not a fake 0");

        const auto write = kernel.write(
                0x1000, std::span<const std::byte>(buffer),
                MemoryChannel::Unavailable);
        ok &= expect(!write.has_value() &&
                             write.error() == CapabilityError::Unsupported,
                     "adapter: write is Unsupported");

        ok &= expect(kernel.state(MemoryChannel::Unavailable) ==
                             CapabilityState::NotSupported,
                     "adapter: default state is NotSupported");
        ok &= expect(!kernel.establish(MemoryChannel::Unavailable,
                                       CarrierKind::Unavailable)
                              .has_value(),
                     "adapter: default establish is Unsupported");
        return ok;
    }

    bool test_w1w2_skips_w3() {
        reset_script();
        const auto result = run_once_with<ghostlock::backend::Cve43499_W1W2>();
        bool ok = true;
        ok &= expect(result.code == ghostlock::pipeline::RunCode::Completed,
                     "w1w2: pipeline completes");
        ok &= expect(count_calls("verify_seccomp") == 0,
                     "w1w2: W3 seccomp probe is never run");
        ok &= expect(count_calls("spawn") == 1, "w1w2: exactly one victim spawn");
        ok &= expect(count_calls("verify_w2") >= 1, "w1w2: W2 verified");
        return ok;
    }
} // namespace

int main() {
    bool ok = true;
    ok &= test_happy_path();
    ok &= test_w2_retry_until_success();
    ok &= test_w2_exhausts_attempts();
    ok &= test_selinux_retry_then_w2();
    ok &= test_capability_adapters();
    ok &= test_w1w2_skips_w3();
    if (ok) {
        std::puts("backend_dataflow_test: ok");
        return 0;
    }
    return 1;
}
