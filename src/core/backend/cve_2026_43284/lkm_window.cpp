/* LKM residency-window runtime -- implementation. See lkm_window.hpp. */

#include "backend/cve_2026_43284/lkm_window.hpp"

#include "backend/cve_2026_43284/diag_line.hpp"
#include "plugin/registry.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        /* The one display token for the capability errors that can end the
         * window's open handshake (design A.3/L9): never a bare number. */
        [[nodiscard]] const char *capability_error_name(
                contract::CapabilityError error) noexcept {
            switch (error) {
            case contract::CapabilityError::None: return "None";
            case contract::CapabilityError::Unsupported: return "Unsupported";
            case contract::CapabilityError::Unavailable: return "Unavailable";
            case contract::CapabilityError::Faulted: return "Faulted";
            case contract::CapabilityError::Rejected: return "Rejected";
            case contract::CapabilityError::Closed: return "Closed";
            }
            return "Unknown";
        }

        void emit_line(DiagLine &line) noexcept {
            (void)std::fputs(line.c_str(), stderr);
            (void)std::fflush(stderr);
        }
    } // namespace

    std::string format_lkm_window_diagnostic(bool opened, bool closed,
                                             std::uint32_t calls,
                                             std::uint32_t abi_version,
                                             bool unload_ok) {
        std::string out("lkm_window opened=");
        out += opened ? '1' : '0';
        out += " closed=";
        out += closed ? '1' : '0';
        out += " calls=";
        out += std::to_string(calls);
        out += " abi_version=";
        out += std::to_string(abi_version);
        out += " unload_ok=";
        out += unload_ok ? '1' : '0';
        out += '\n';
        return out;
    }

    void LkmWindowRuntime::emit_diagnostic(bool unload_ok) noexcept {
        const std::string line = format_lkm_window_diagnostic(
                opened_, closed_, calls(), abi_version(), unload_ok);
        (void)std::fputs(line.c_str(), stdout);
        (void)std::fflush(stdout);
    }

    bool LkmWindowRuntime::open() noexcept {
        if (closed_) {
            return false;
        }
        if (opened_) {
            return true;
        }
        if (channel_) {
            return false;
        }
        /* The injected transport is a host-test seam; production leaves it
         * unavailable and binds the real /dev/glk device. */
        const plugin::LkmTransport transport =
                injected_.available() ? injected_ : device_.transport();
        channel_.emplace(transport);

        contract::CapabilityStatus status{};
        std::uint32_t attempts = 0U;
        for (std::uint32_t attempt = 0U; attempt <= open_retry_attempts_; ++attempt) {
            ++attempts;
            status = channel_->establish();
            if (status.has_value()) {
                break;
            }
            if (status.error() != contract::CapabilityError::Unavailable) {
                /* Unsupported (ABI mismatch) and Closed are hard failures: never
                 * guess across an incompatible or spent channel. */
                break;
            }
            if (attempt < open_retry_attempts_) {
                std::this_thread::sleep_for(
                        std::chrono::milliseconds(open_retry_delay_ms_));
            }
        }
        if (!status.has_value()) {
            DiagLine line("window");
            line.n("result", "open_failed")
                    .u("attempts", attempts)
                    .n("reason", capability_error_name(status.error()));
            emit_line(line);
            channel_.reset();
            emit_diagnostic(false);
            return false;
        }
        {
            /* The ABI version is reported once by the lkm_window diagnostic at
             * close(); here the handshake cost is what matters. */
            DiagLine line("window");
            line.n("result", "opened").u("attempts", attempts);
            emit_line(line);
        }
        memory_.emplace(*channel_);
        alias_.emplace(*channel_);
        capabilities_.kernel = &*memory_;
        capabilities_.alias = &*alias_;
        /* Publish the plugin-facing projection of the same capabilities. The
         * context stays alive after close so a late call is a clean -EBADF. */
        host_ctx_.capabilities = &capabilities_;
        host_ctx_.channel = contract::MemoryChannel::LkmProxy;
        host_ctx_.closed = false;
        plugin::init_host_ops(host_ops_, host_ctx_);
        opened_ = true;
        return true;
    }

    bool LkmWindowRuntime::run() noexcept {
        /* run() is a window-body call; a closed window is terminal, so it can no
         * more dispatch hooks than a never-opened one. */
        if (!opened_ || closed_) {
            return false;
        }
        ++window_calls_;
        /* S4 P1 step 3a: the wired POST_TERMINAL consumer is the production
         * path. It is fail-soft by contract (design section 5): the host counts
         * hook failures and disables only the failing module, so run() never
         * reports a plugin failure to the chain. It is only reached here, while
         * the residency window is open. */
        if (plugin_stage_.available()) {
            plugin_stage_.dispatch(plugin_stage_.ctx, &host_ops_);
            return true;
        }
        if (registry_ == nullptr) {
            return true;
        }
        /* POST_TERMINAL is the one stage inside the LKM residency window
         * (contract-design.md 3.13): the chain reaches run() only after
         * WaitResult reports LkmLoaded and before Cleanup, and PRE_TERMINAL is
         * before the load, so it is not window-legal. Hooks run synchronously,
         * one at a time; a failure is recorded (and the module disabled) by the
         * plugin-side traversal, and run() then reports failure so the chain's
         * single terminus still closes the window. */
        const plugin::RegistryRunOutcome outcome = plugin::run_registry_stage(
                *registry_, contract::CountermeasureStage::PostTerminal, &host_ops_);
        hook_calls_ += outcome.called;
        hook_failures_ += outcome.failed;
        return outcome.ok();
    }

    void LkmWindowRuntime::close() noexcept {
        if (closed_) {
            return;
        }
        closed_ = true;
        bool unload_ok = false;
        if (channel_) {
            const contract::CapabilityStatus status = channel_->release();
            unload_ok = status.has_value();
        }
        if (opened_) {
            emit_diagnostic(unload_ok);
        }
        /* The window is over: move the plugin-facing context to its terminal
         * state first, so any call through a retained glk_contract_ops pointer
         * returns -EBADF instead of reading a cleared capability view. */
        plugin::close_host_ops(host_ctx_);
        host_ops_.child_task = 0U;
        /* Clear the per-run view so no plugin can consume a Closed capability.
         * The adapters stay alive and report Closed. */
        capabilities_ = contract::Capabilities{};
    }

    std::uint32_t LkmWindowRuntime::calls() const noexcept {
        return channel_ ? channel_->stats().calls : 0U;
    }

    std::uint32_t LkmWindowRuntime::abi_version() const noexcept {
        return channel_ ? channel_->abi_version() : 0U;
    }

} // namespace ghostlock::backend::cve_2026_43284
