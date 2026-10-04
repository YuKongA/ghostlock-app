/* Host contract test for the B5-8 umh_forward terminal policy.
 *
 * The kernel UMH channel is an injected fake, so nothing touches a device,
 * forks or writes a file. Every fail-closed path and the positive forward/wait
 * path are exercised against the real production unit. */

#include "terminal/umh_forward.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

using ghostlock::session::StageResult;
using ghostlock::terminal::run_umh_forward;
using ghostlock::terminal::RootProgram;
using ghostlock::terminal::RootProgramKind;
using ghostlock::terminal::UmhCommand;
using ghostlock::terminal::UmhForwardChannel;
using ghostlock::terminal::UmhForwardInput;
using ghostlock::terminal::UmhForwardOutcome;
using ghostlock::terminal::UmhReadyState;

namespace {

    struct FakeUmh final {
        UmhForwardOutcome outcome = UmhForwardOutcome::Ready;
        UmhReadyState ready = UmhReadyState::Ready;
        std::uint32_t calls = 0U;
        std::uint32_t ready_calls = 0U;
        std::uint32_t seen_timeout = 0U;
        std::string_view seen_program{};
        std::size_t seen_argc = 0U;
    };

    UmhForwardOutcome fake_forward(void *ctx, const RootProgram &program,
                                   const UmhCommand &command,
                                   std::uint32_t wait_timeout_ms) noexcept {
        auto *f = static_cast<FakeUmh *>(ctx);
        ++f->calls;
        f->seen_timeout = wait_timeout_ms;
        f->seen_program = program.argv_view();
        f->seen_argc = command.argc;
        return f->outcome;
    }

    UmhReadyState fake_ready(void *ctx) noexcept {
        auto *f = static_cast<FakeUmh *>(ctx);
        ++f->ready_calls;
        return f->ready;
    }

    /* A fully-populated backend handoff; tests knock out one field at a time. */
    UmhForwardInput make_input(FakeUmh &fake, RootProgramKind kind = RootProgramKind::KernelSU,
                               bool bind_channel = true, bool bind_probe = true) {
        UmhForwardInput in{};
        in.lkm_loaded = true;
        in.root_program.kind = kind;
        in.root_program.set_argv("/data/adb/ksud");
        in.command.argc = 2U;
        std::strncpy(in.command.argv[0].data(), "/data/adb/ksud",
                     in.command.argv[0].size() - 1U);
        std::strncpy(in.command.argv[1].data(), "late-load",
                     in.command.argv[1].size() - 1U);
        in.session_secrets = &fake;
        in.session_secrets_size = sizeof(fake);
        if (bind_channel) {
            in.channel.ctx = &fake;
            in.channel.forward = fake_forward;
            in.channel.wait_timeout_ms = 4321U;
        }
        if (bind_probe) {
            in.channel.ready_ctx = &fake;
            in.channel.ready = fake_ready;
        }
        return in;
    }

    void test_positive_ksud() {
        FakeUmh fake{};
        UmhForwardInput in = make_input(fake);
        assert(run_umh_forward(in) == StageResult::Done);
        assert(fake.calls == 1U);
        assert(fake.ready_calls == 1U);
        assert(fake.seen_timeout == 4321U);
        assert(fake.seen_program == "/data/adb/ksud");
        assert(fake.seen_argc == 2U);
    }

    void test_non_ksud_needs_no_probe() {
        FakeUmh fake{};
        UmhForwardInput in =
                make_input(fake, RootProgramKind::FolkPatch, true, false);
        assert(run_umh_forward(in) == StageResult::Done);
        assert(fake.calls == 1U);
        assert(fake.ready_calls == 0U);
    }

    void test_fail_closed_terminus_and_channel() {
        FakeUmh fake{};
        {
            UmhForwardInput in = make_input(fake);
            in.lkm_loaded = false;
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(fake);
            in.command.argc = 0U;
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(fake);
            in.root_program.set_argv("");
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(fake);
            in.session_secrets = nullptr;
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(fake);
            in.session_secrets_size = 0U;
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(fake, RootProgramKind::KernelSU, false);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 0U);
        }
    }

    void test_forward_outcomes() {
        for (const UmhForwardOutcome outcome :
             {UmhForwardOutcome::Rejected, UmhForwardOutcome::Timeout,
              UmhForwardOutcome::Failed}) {
            FakeUmh fake{};
            fake.outcome = outcome;
            UmhForwardInput in = make_input(fake);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 1U);
            assert(fake.ready_calls == 0U);
        }
    }

    void test_ksud_readiness_probe() {
        {
            FakeUmh fake{};
            UmhForwardInput in =
                    make_input(fake, RootProgramKind::KernelSU, true, false);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.calls == 1U);
        }
        for (const UmhReadyState ready :
             {UmhReadyState::NotReady, UmhReadyState::Unavailable}) {
            FakeUmh fake{};
            fake.ready = ready;
            UmhForwardInput in = make_input(fake);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(fake.ready_calls == 1U);
        }
    }

} // namespace

int main() {
    test_positive_ksud();
    test_non_ksud_needs_no_probe();
    test_fail_closed_terminus_and_channel();
    test_forward_outcomes();
    test_ksud_readiness_probe();
    std::puts("umh_forward_test: ok");
    return 0;
}
