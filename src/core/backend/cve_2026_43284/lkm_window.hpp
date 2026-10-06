#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_WINDOW_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_WINDOW_HPP

/* LKM residency-window runtime for the CVE-2026-43284 chain (delta-2).
 *
 * The chain (steps/chain.hpp) exposes three optional callbacks on ChainOps:
 * open_lkm_channel, run_lkm_window and close_lkm_channel. The chain calls open
 * exactly once after WaitResult reports LkmLoaded, then run (the POST_TERMINAL
 * consumer slot) and close in finish() on every path. This unit owns the state
 * those callbacks need: the /dev/glk device binding, the versioned LkmChannel
 * (PING-checked), the two contract adapters and the per-run Capabilities the
 * window publishes to plugins/steps.
 *
 * Lifecycle (design 3.12.1/3.12.3): NotOpened -> Opened -> Closed. Opened
 * requires a resident module and a matching ABI; Closed is terminal and the
 * adapters report CapabilityState::Closed afterwards, so a released channel can
 * never be reused. open() never turns a failure into a success (R7).
 *
 * The device boundary is injected (set_test_transport) so a host without
 * /dev/glk can drive the window with a fake ioctl backend; production leaves
 * the injection unavailable and uses plugin::LkmDeviceBinding. open() retries
 * an Unavailable device a bounded number of times because the module registers
 * /dev/glk only after the late-load command has created the success marker the
 * chain waits on; an ABI mismatch is Unsupported and never retried.
 *
 * delta-4: run() also dispatches the caller-attached plugin registry. The LKM
 * residency window is the only point where POST_TERMINAL hooks may run
 * (contract-design.md 3.13: WaitResult=LkmLoaded, before Cleanup), so those
 * hooks are invoked synchronously, one at a time, with a glk_contract_ops view
 * of this window's Capabilities. A non-zero hook return is recorded in the
 * registry and run() reports failure; the chain terminus still closes the
 * window, so a failing plugin can never leak the residency window.
 *
 * S4 P1 step 3a: when the composition seam attaches a PluginStageSink, run()
 * invokes it instead, with the same glk_contract_ops view; the sink dispatches
 * the P1 host's POST_TERMINAL stage. That path is fail-soft by contract (design
 * section 5): the host counts hook failures and disables only the failing
 * module, so run() stays a window-body success and no plugin can fail the chain.
 *
 * R1: backend -> {contract, plugin}; this header includes no pipeline/terminal. */

#include "backend/cve_2026_43284_state.hpp"
#include "contract/capabilities.hpp"
#include "plugin/host_ops.hpp"
#include "plugin/kernel_channel.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace ghostlock::plugin {
    class RuntimeRegistry;
} // namespace ghostlock::plugin

namespace ghostlock::backend::cve_2026_43284 {

    /* Formats the one-line window diagnostic in the existing stdout style (no
     * new logging framework). Always ends with a newline. */
    [[nodiscard]] std::string format_lkm_window_diagnostic(bool opened, bool closed,
                                                           std::uint32_t calls,
                                                           std::uint32_t abi_version,
                                                           bool unload_ok);

    /* Owns the LKM window for one run. Non-copyable and must not move once the
     * ChainOps callbacks were bound to it; the composition root keeps it in
     * ProductionResources (production) or the staged runner frame (staged). */
    class LkmWindowRuntime final {
    public:
        LkmWindowRuntime() noexcept = default;
        ~LkmWindowRuntime() noexcept = default;
        LkmWindowRuntime(const LkmWindowRuntime &) = delete;
        LkmWindowRuntime &operator=(const LkmWindowRuntime &) = delete;

        /* Host-test seam. A fully-bound transport wins over the production
         * /dev/glk binding; production never calls this. */
        void set_test_transport(plugin::LkmTransport transport) noexcept {
            injected_ = transport;
        }

        /* S4 R4: the profile can tune the module handshake poll (how many times
         * and how long to wait for /dev/glk to appear after the late-load
         * marker). The defaults are the values that used to be private
         * constants here. Must be set before open(). */
        void set_handshake_policy(std::uint32_t attempts,
                                  std::uint32_t interval_ms) noexcept {
            open_retry_attempts_ = attempts;
            open_retry_delay_ms_ = interval_ms;
        }

        /* Borrows a caller-owned registry for this run. The caller must keep it
         * alive for at least as long as this runtime. A null registry (the
         * default) keeps run() a pure window-body counter, so a run with no
         * plugin behaves exactly as before. */
        void attach_registry(plugin::RuntimeRegistry *registry) noexcept {
            registry_ = registry;
        }

        /* S4 P1 step 3a: the POST_TERMINAL consumer, expressed as a neutral
         * thunk so this backend unit never names the P1 host (the same idiom as
         * ChainOps / LkmTransport). The composition seam binds it; the caller
         * owns ctx and keeps it alive across the window. An unavailable sink
         * (the default) leaves run() exactly as it was. */
        struct PluginStageSink final {
            void (*dispatch)(void *ctx, const glk_contract_ops *ops) noexcept = nullptr;
            void *ctx = nullptr;

            [[nodiscard]] bool available() const noexcept {
                return dispatch != nullptr;
            }
        };

        void attach_plugin_stage(PluginStageSink sink) noexcept {
            plugin_stage_ = sink;
        }

        /* Opens + PINGs the channel and, on success, fills capabilities().
         * Idempotent while open; a closed window stays Closed. Fail-closed: a
         * failed open/PING leaves the capability view empty and returns false. */
        [[nodiscard]] bool open() noexcept;

        /* POST_TERMINAL consumer slot. Today an explicit wiring point for
         * plugins; it counts invocations and fails closed when no window is
         * open. */
        [[nodiscard]] bool run() noexcept;

        /* Sends UNLOAD, drops the device and moves to the Closed terminal state.
         * Idempotent; prints the lkm_window diagnostic once. */
        void close() noexcept;

        [[nodiscard]] bool is_open() const noexcept { return opened_; }
        [[nodiscard]] bool is_closed() const noexcept { return closed_; }
        /* Number of forwarded channel primitives (LkmWindowStats::calls). */
        [[nodiscard]] std::uint32_t calls() const noexcept;
        /* Number of run() invocations (the window body runs). */
        [[nodiscard]] std::uint32_t window_calls() const noexcept { return window_calls_; }
        [[nodiscard]] std::uint32_t abi_version() const noexcept;
        /* Per-run capability view; non-null only while the window is open. */
        [[nodiscard]] const contract::Capabilities &capabilities() const noexcept {
            return capabilities_;
        }
        [[nodiscard]] contract::Capabilities &capabilities() noexcept {
            return capabilities_;
        }
        /* Adapter accessors for tests/diagnostics; the objects stay alive after
         * close() so they can report CapabilityState::Closed. */
        [[nodiscard]] plugin::LkmProxyKernelMemory *memory() noexcept {
            return memory_ ? &*memory_ : nullptr;
        }
        [[nodiscard]] plugin::LkmProxyKernelAlias *alias() noexcept {
            return alias_ ? &*alias_ : nullptr;
        }
        /* The glk_contract_ops view handed to every plugin hook. Valid for the
         * whole runtime lifetime: before open it is a closed, empty context, so
         * any call fails closed instead of dereferencing a null capability. */
        [[nodiscard]] const glk_contract_ops *host_ops() const noexcept {
            return &host_ops_;
        }
        /* Real hook invocations and non-zero hook returns during run(). */
        [[nodiscard]] std::uint32_t hook_calls() const noexcept {
            return hook_calls_;
        }
        [[nodiscard]] std::uint32_t hook_failures() const noexcept {
            return hook_failures_;
        }

    private:
        /* S4 R4 handshake poll tuning; profile-driven, defaulting to the old
         * private constants (40 attempts x 5 ms). */
        std::uint32_t open_retry_attempts_ =
                kCve2026_43284ModulePollAttemptsDefault;
        std::uint32_t open_retry_delay_ms_ =
                kCve2026_43284ModulePollIntervalMsDefault;

        void emit_diagnostic(bool unload_ok) noexcept;

        plugin::LkmDeviceBinding device_{};
        plugin::LkmTransport injected_{};
        std::optional<plugin::LkmChannel> channel_{};
        std::optional<plugin::LkmProxyKernelMemory> memory_{};
        std::optional<plugin::LkmProxyKernelAlias> alias_{};
        contract::Capabilities capabilities_{};
        /* Non-owning: the context stays alive after close() so a late call
         * through host_ops() reports -EBADF rather than touching freed state. */
        plugin::HostOpsContext host_ctx_{};
        glk_contract_ops host_ops_{};
        plugin::RuntimeRegistry *registry_ = nullptr;
        /* S4 P1 step 3a: the wired POST_TERMINAL consumer. When available it
         * takes precedence over the legacy registry seam (production never
         * attaches both). */
        PluginStageSink plugin_stage_{};
        std::uint32_t window_calls_ = 0U;
        std::uint32_t hook_calls_ = 0U;
        std::uint32_t hook_failures_ = 0U;
        bool opened_ = false;
        bool closed_ = false;
    };

} // namespace ghostlock::backend::cve_2026_43284

#endif
