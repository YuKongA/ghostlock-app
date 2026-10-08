/* Host tests for the delta-2 LKM residency window (lkm_window.{hpp,cpp}).
 *
 * The window binds the chain's three LKM callbacks to a versioned /dev/glk
 * channel. The host has no /dev/glk, so a fake LkmTransport is injected through
 * LkmWindowRuntime::set_test_transport; the production LkmDeviceBinding failure
 * path (no device -> Unavailable) is covered as well. The assertions are:
 *   - open() fills the per-run Capabilities with the two adapters;
 *   - run() counts window-body invocations;
 *   - a forwarded primitive increments LkmWindowStats::calls;
 *   - close() sends UNLOAD, clears the Capabilities and leaves the adapters
 *     reporting CapabilityState::Closed (terminal, no reuse);
 *   - a PING/ABI mismatch is fail-closed and never opens;
 *   - the diagnostic line is exactly lkm_window opened=... calls=...;
 *   - delta-4: an attached registry's POST_TERMINAL hooks run synchronously
 *     through the published glk_contract_ops (a real hook calls write_bytes and
 *     read_u64, the fake LKM transport records the ops) and a retained ops
 *     pointer reports -EBADF after close. */

#include "backend/cve_2026_43284/lkm_window.hpp"

#include "contract/abi/glk_contract_abi.h"
#include "plugin/registry.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

namespace {

    using ghostlock::backend::cve_2026_43284::format_lkm_window_diagnostic;
    using ghostlock::backend::cve_2026_43284::LkmWindowRuntime;
    using ghostlock::contract::CapabilityState;
    using ghostlock::contract::MemoryChannel;
    using ghostlock::plugin::LkmTransport;

    struct Fake final {
        std::array<std::uint8_t, 8192> kernel_mem{};
        std::uint64_t direct_base = 0xFFFF000000000000ULL;
        std::uint32_t direct_span = 8192U;

        int open_result = 0;
        bool ping_version_mismatch = false;

        bool opened = false;
        bool closed = false;
        bool unloaded = false;
        std::uint32_t ioctl_calls = 0U;
        /* delta-4: per-op evidence that a hook really went through the channel. */
        std::uint32_t read_ops = 0U;
        std::uint32_t write_ops = 0U;
        std::uint32_t write_zero_ops = 0U;
        std::uint32_t direct_map_ops = 0U;
        std::uint32_t last_op = 0xFFFFFFFFU;
    };

    Fake g;

    [[nodiscard]] std::uint8_t *kernel_at(std::uint64_t addr) noexcept {
        return g.kernel_mem.data() + (addr - g.direct_base);
    }

    [[nodiscard]] bool in_direct(std::uint64_t addr, std::uint32_t len) noexcept {
        return addr >= g.direct_base &&
               (addr - g.direct_base) + static_cast<std::uint64_t>(len) <=
                       static_cast<std::uint64_t>(g.direct_span);
    }

    int fake_open(void *ctx) noexcept {
        (void)ctx;
        g.opened = true;
        g.closed = false;
        return g.open_result;
    }

    void fake_close(void *ctx) noexcept {
        (void)ctx;
        g.closed = true;
    }

    int fake_call(void *ctx, glk_lkm_req &req) noexcept {
        (void)ctx;
        ++g.ioctl_calls;
        if (req.abi_version != GLK_LKM_ABI_VERSION) {
            req.status = static_cast<std::uint32_t>(-EPROTO);
            return 0;
        }
        req.status = 0U;
        g.last_op = req.op;
        switch (req.op) {
        case GLK_LKM_PING:
            if (g.ping_version_mismatch) {
                req.status = static_cast<std::uint32_t>(-EPROTO);
            }
            break;
        case GLK_LKM_READ:
            ++g.read_ops;
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memcpy(reinterpret_cast<void *>(req.value), kernel_at(req.addr),
                        static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_WRITE:
            ++g.write_ops;
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memcpy(kernel_at(req.addr), reinterpret_cast<const void *>(req.value),
                        static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_WRITE_ZERO:
            ++g.write_zero_ops;
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memset(kernel_at(req.addr), 0, static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_DIRECT_MAP:
            ++g.direct_map_ops;
            req.addr = g.direct_base; /* a provable alias for this fake */
            break;
        case GLK_LKM_UNLOAD:
            g.unloaded = true;
            break;
        default:
            req.status = static_cast<std::uint32_t>(-EOPNOTSUPP);
            break;
        }
        return 0;
    }

    [[nodiscard]] LkmTransport transport() noexcept {
        LkmTransport t{};
        t.ctx = &g;
        t.open = &fake_open;
        t.close = &fake_close;
        t.call = &fake_call;
        return t;
    }

    void reset() noexcept { g = Fake{}; }

    /* ---- delta-4: a real C-function hook that exercises the channel ---- */

    int g_hook_calls = 0;
    const glk_contract_ops *g_last_ops = nullptr;

    std::int32_t channel_hook(void *user, glk_stage stage,
                              const glk_contract_ops *host) {
        (void)user;
        assert(stage == GLK_STAGE_POST_TERMINAL);
        ++g_hook_calls;
        g_last_ops = host;
        const std::uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        const std::int32_t w =
                host->write_bytes(host->ctx, g.direct_base, payload,
                              static_cast<std::uint32_t>(sizeof(payload)));
        std::uint64_t value = 0U;
        const std::int32_t r =
                host->read_u64(host->ctx, g.direct_base, &value);
        return (w == 0 && r == 0) ? 0 : -1;
    }

    int g_fail_calls = 0;
    int g_fail_second_calls = 0;

    std::int32_t failing_hook(void *, glk_stage, const glk_contract_ops *) {
        ++g_fail_calls;
        return -7;
    }

    std::int32_t failing_hook_second(void *, glk_stage, const glk_contract_ops *) {
        ++g_fail_second_calls;
        return 0;
    }

    constexpr ghostlock::contract::Capability kWindowCaps =
            ghostlock::contract::Capability::KernelRead |
            ghostlock::contract::Capability::KernelWrite |
            ghostlock::contract::Capability::Alias;

    void test_window_open_run_close() {
        reset();
        LkmWindowRuntime window{};
        window.set_test_transport(transport());

        assert(!window.is_open());
        assert(window.memory() == nullptr);
        assert(window.capabilities().kernel == nullptr);

        assert(window.open());
        assert(window.is_open());
        assert(!window.is_closed());
        assert(window.capabilities().kernel != nullptr);
        assert(window.capabilities().alias != nullptr);
        assert(window.memory() != nullptr);
        assert(window.alias() != nullptr);
        assert(window.abi_version() == GLK_LKM_ABI_VERSION);
        assert(window.memory()->state(MemoryChannel::LkmProxy) ==
               CapabilityState::Available);

        /* run() is the POST_TERMINAL consumer slot and counts invocations. */
        assert(window.window_calls() == 0U);
        assert(window.run());
        assert(window.run());
        assert(window.window_calls() == 2U);

        /* A primitive forwarded through the capability view reaches the channel
         * and bumps the forwarded-call statistic. */
        std::array<std::byte, 8> buffer{};
        const auto read = window.capabilities().kernel->read(
                g.direct_base, std::span<std::byte>(buffer), MemoryChannel::LkmProxy);
        assert(read.has_value());
        assert(window.calls() == 1U);

        window.close();
        assert(window.is_closed());
        assert(g.unloaded);
        assert(g.closed);
        /* The per-run view is cleared; the adapters report the terminal Closed
         * state and are not reusable. */
        assert(window.capabilities().kernel == nullptr);
        assert(window.capabilities().alias == nullptr);
        assert(window.memory()->state(MemoryChannel::LkmProxy) ==
               CapabilityState::Closed);
        assert(!window.memory()->supports(MemoryChannel::LkmProxy));
        assert(!window.alias()->to_direct_map(0x1000U).has_value());
        std::array<std::byte, 4> after{};
        const auto closed_read = window.memory()->read(
                g.direct_base, std::span<std::byte>(after), MemoryChannel::LkmProxy);
        assert(!closed_read.has_value());

        /* Terminal: no window may be re-established and close is idempotent. */
        assert(!window.open());
        window.close();
        assert(window.is_closed());
    }

    void test_ping_mismatch_fails_closed() {
        reset();
        g.ping_version_mismatch = true;
        LkmWindowRuntime window{};
        window.set_test_transport(transport());

        assert(!window.open());
        assert(!window.is_open());
        assert(window.memory() == nullptr);
        assert(window.capabilities().kernel == nullptr);
        /* The transport device was dropped when PING failed. */
        assert(g.closed);
        assert(!window.run());
    }

    void test_device_binding_failure_is_not_available() {
        reset();
        /* No injected transport: the production LkmDeviceBinding path is used.
         * On the non-Linux host it returns -ENOSYS; on a Linux host without
         * /dev/glk the open fails. Either way the window must stay closed. */
        LkmWindowRuntime window{};
        assert(!window.open());
        assert(!window.is_open());
        assert(window.memory() == nullptr);
        assert(window.capabilities().kernel == nullptr);
        assert(!window.run());
        window.close();
        assert(window.is_closed());
    }

    void test_diagnostic_format() {
        const std::string ok =
                format_lkm_window_diagnostic(true, true, 7U, GLK_LKM_ABI_VERSION, true);
        assert(ok == "lkm_window opened=1 closed=1 calls=7 abi_version=1 unload_ok=1\n");

        const std::string fail =
                format_lkm_window_diagnostic(false, false, 0U, 0U, false);
        assert(fail ==
               "lkm_window opened=0 closed=0 calls=0 abi_version=0 unload_ok=0\n");
    }

    void test_registry_hooks_run_through_channel() {
        reset();
        g_hook_calls = 0;
        g_last_ops = nullptr;

        glk_hook hooks[] = {
            {GLK_TRIGGER_ON_STAGE, GLK_STAGE_POST_TERMINAL, 0U, 0U, &channel_hook,
             nullptr, "window.channel"},
        };
        ghostlock::plugin::RuntimeRegistry registry(
                kWindowCaps, ghostlock::contract::kHostImplementedTriggers);
        assert(registry.register_module(ghostlock::plugin::ExternalModuleBinding{
                "ghostlock.window.test", "1.0.0", kWindowCaps, hooks, 1U}));

        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_registry(&registry);

        assert(window.open());
        assert(window.run());
        assert(window.window_calls() == 1U);
        assert(window.hook_calls() == 1U);
        assert(window.hook_failures() == 0U);
        assert(g_hook_calls == 1);
        assert(g_last_ops == window.host_ops());
        /* The hook really reached the LKM transport through the channel. */
        assert(g.write_ops >= 1U);
        assert(g.read_ops >= 1U);
        assert(window.calls() >= 2U);
        assert(registry.stage_failures() == 0U);

        /* A retained ops pointer is terminal after close. */
        window.close();
        assert(window.is_closed());
        std::uint64_t value = 0U;
        assert(g_last_ops != nullptr);
        assert(g_last_ops->read_u64(g_last_ops->ctx, g.direct_base, &value) == -EBADF);
        assert(g_last_ops->write_u64(g_last_ops->ctx, g.direct_base, 1U) == -EBADF);
        const std::uint8_t byte = 0U;
        assert(g_last_ops->write_bytes(g_last_ops->ctx, g.direct_base, &byte, 1U) ==
               -EBADF);
        assert(g_last_ops->image_to_direct_map(g_last_ops->ctx, 0x1000U) == 0U);
        assert(!window.run());
    }

    void test_hook_failure_records_and_closes() {
        reset();
        g_fail_calls = 0;
        g_fail_second_calls = 0;
        glk_hook hooks[] = {
            {GLK_TRIGGER_ON_STAGE, GLK_STAGE_POST_TERMINAL, 0U, 0U, &failing_hook,
             nullptr, "window.fail"},
            {GLK_TRIGGER_ON_STAGE, GLK_STAGE_POST_TERMINAL, 0U, 0U,
             &failing_hook_second, nullptr, "window.fail.second"},
        };
        ghostlock::plugin::RuntimeRegistry registry(
                kWindowCaps, ghostlock::contract::kHostImplementedTriggers);
        assert(registry.register_module(ghostlock::plugin::ExternalModuleBinding{
                "ghostlock.window.fail", "1.0.0", kWindowCaps, hooks, 2U}));

        LkmWindowRuntime window{};
        window.set_test_transport(transport());
        window.attach_registry(&registry);
        assert(window.open());

        /* Failure is recorded, the module is disabled (its second hook is
         * skipped) and run() reports failure so the chain aborts - but the
         * terminus still closes the window, so the module never leaks. */
        assert(!window.run());
        assert(window.window_calls() == 1U);
        assert(window.hook_calls() == 1U);
        assert(window.hook_failures() == 1U);
        assert(g_fail_calls == 1);
        assert(g_fail_second_calls == 0);
        assert(registry.stage_failures() == 1U);
        assert(registry.modules_skipped() == 1U);

        window.close();
        assert(window.is_closed());
        assert(g.unloaded);
        assert(g.closed);
        assert(!window.run());
    }

} // namespace

int main() {
    test_window_open_run_close();
    test_ping_mismatch_fails_closed();
    test_device_binding_failure_is_not_available();
    test_diagnostic_format();
    test_registry_hooks_run_through_channel();
    test_hook_failure_records_and_closes();
    std::puts("cve_2026_43284_lkm_window_test: OK");
    return 0;
}
