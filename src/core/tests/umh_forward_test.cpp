/* Host contract test for the B6/T5 umh_forward terminal policy.
 *
 * The readiness probe is an injected fake, so nothing touches a device, forks
 * or writes a file. The terminal is read-only: it confirms the LKM/UMH
 * readiness markers and never forwards the UMH command or reads the session
 * secrets. Every fail-closed path and the positive Ready path are exercised
 * against the real production unit. */

#include "terminal/umh_forward.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

using ghostlock::contract::StageResult;
using ghostlock::terminal::production_umh_channel;
using ghostlock::terminal::run_umh_forward;
using ghostlock::terminal::UmhForwardChannel;
using ghostlock::terminal::UmhForwardInput;
using ghostlock::terminal::UmhReadyState;

namespace {

    struct FakeProbe final {
        UmhReadyState ready = UmhReadyState::Ready;
        std::uint32_t calls = 0U;
        void *seen_ctx = nullptr;
    };

    UmhReadyState fake_ready(void *ctx) noexcept {
        auto *f = static_cast<FakeProbe *>(ctx);
        ++f->calls;
        f->seen_ctx = ctx;
        return f->ready;
    }

    /* The terminal only needs the terminus flag and a bound readiness probe. The
     * legacy command/argv/secrets fields are deliberately left empty to prove
     * the terminal does not gate on them (they are chain/composition-root state). */
    UmhForwardInput make_input(FakeProbe &probe, bool bind_probe = true) {
        UmhForwardInput in{};
        in.lkm_loaded = true;
        if (bind_probe) {
            in.channel.ctx = &probe;
            in.channel.ready = fake_ready;
        }
        return in;
    }

    void test_positive_ready() {
        FakeProbe probe{};
        UmhForwardInput in = make_input(probe);
        assert(run_umh_forward(in) == StageResult::Done);
        assert(probe.calls == 1U);
        assert(probe.seen_ctx == &probe);
    }

    void test_read_only_ignores_command_and_secrets() {
        /* Empty command/argv and no session secrets must still succeed when the
         * probe reports Ready: the chain consumed the secrets and completed the
         * load; the terminal only confirms readiness. */
        FakeProbe probe{};
        UmhForwardInput in = make_input(probe);
        in.command.argc = 0U;
        in.root_program.set_argv("");
        in.session_secrets = nullptr;
        in.session_secrets_size = 0U;
        assert(run_umh_forward(in) == StageResult::Done);
        assert(probe.calls == 1U);
    }

    void test_fail_closed_terminus_and_channel() {
        FakeProbe probe{};
        {
            UmhForwardInput in = make_input(probe);
            in.lkm_loaded = false;
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(probe.calls == 0U);
        }
        {
            UmhForwardInput in = make_input(probe, false);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(probe.calls == 0U);
        }
    }

    void test_probe_states() {
        for (const UmhReadyState ready :
             {UmhReadyState::NotReady, UmhReadyState::Unavailable}) {
            FakeProbe probe{};
            probe.ready = ready;
            UmhForwardInput in = make_input(probe);
            assert(run_umh_forward(in) == StageResult::Failed);
            assert(probe.calls == 1U);
        }
    }

    void test_production_channel_host_binding() {
        /* The production channel is valid on Linux (the probe is bound) and
         * invalid elsewhere (fail-closed); both are compiled-in behavior. */
        const UmhForwardChannel channel = production_umh_channel();
#if defined(__linux__)
        assert(channel.valid());
        assert(channel.ready != nullptr);
#else
        assert(!channel.valid());
#endif
    }

} // namespace

int main() {
    test_positive_ready();
    test_read_only_ignores_command_and_secrets();
    test_fail_closed_terminus_and_channel();
    test_probe_states();
    test_production_channel_host_binding();
    std::puts("umh_forward_test: ok");
    return 0;
}
