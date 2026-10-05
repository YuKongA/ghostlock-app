#ifndef GHOSTLOCK_TERMINAL_TERMINAL_INPUT_HPP
#define GHOSTLOCK_TERMINAL_TERMINAL_INPUT_HPP

/* Concrete terminal input payloads. The neutral base (TerminalInput,
 * ActivationContext, RootProgram) is identity/interface vocabulary and lives in
 * contract/identity.hpp (ADR-0004) so the execution contracts can name it
 * without a contract -> terminal include edge; it is re-exported here under
 * ghostlock::terminal for the terminal-facing call sites. */

#include "contract/identity.hpp"
#include "terminal/root_program.hpp"
#include "terminal/umh_command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::terminal {
    using contract::ActivationContext;
    using contract::TerminalInput;

    /* Where the kernel module image came from; neutral mirror of the backend's
     * lkm_path token so the terminal need not include a backend header. */
    enum class UmhLkmSource : std::uint8_t { None = 0, BundledKmi = 1, CustomFile = 2 };

    inline constexpr std::size_t kUmhKmiLabelBytes = 32U;
    inline constexpr std::size_t kUmhCarrierPathBytes = 64U;

    /* Result of forwarding the App-selected root program to the kernel UMH
     * channel. Rejected/Timeout/Failed are all terminal failures; only Ready lets
     * the terminal report success. */
    enum class UmhForwardOutcome : std::uint8_t {
        Ready = 0,
        Rejected,
        Timeout,
        Failed,
    };

    /* One forward attempt: write the backend-built UMH command to the channel and
     * wait up to wait_timeout_ms for the kernel helper to run it. Returned by the
     * injected channel; the production binding lands in B5-9. */
    using UmhForwardFn = UmhForwardOutcome (*)(void *ctx,
                                               const RootProgram &root_program,
                                               const UmhCommand &command,
                                               std::uint32_t wait_timeout_ms) noexcept;

    /* Post-forward readiness of the selected program. KernelSU readiness is a
     * device fact (handoff_probe); the terminal only consults it when the App
     * selected a ksud-style program and a probe is bound. */
    enum class UmhReadyState : std::uint8_t {
        Ready = 0,
        NotReady,
        Unavailable,
    };

    using UmhReadyFn = UmhReadyState (*)(void *ctx) noexcept;

    /* Neutral UMH channel handle (ADR-0004 R19): the backend copies the handle it
     * was handed into UmhForwardInput after a clean terminus; the umh_forward
     * terminal performs the forward/wait. A null handle fails closed, so an
     * unbound channel can never be treated as success. Function pointers keep the
     * handle host-testable without a device; the kernel binding is B5-9. */
    struct UmhForwardChannel final {
        void *ctx = nullptr;
        UmhForwardFn forward = nullptr;
        void *ready_ctx = nullptr;
        UmhReadyFn ready = nullptr;
        std::uint32_t wait_timeout_ms = 5000U;

        [[nodiscard]] bool valid() const noexcept {
            return ctx != nullptr && forward != nullptr;
        }
    };

    /* Input for the umh_forward terminal: the backend reports the UMH/LKM state
     * and the terminal forwards to the selected root program.
     *
     * B5-7 backend handoff. The cve_2026_43284 backend fills every field after
     * its endgame terminus; the terminal only reads them. session_secrets is an
     * opaque, non-owning reference to the backend-owned IpsecSaParams: it is
     * valid through Terminal::run and is zeroized by the backend state
     * destructor. The terminal must never persist, copy to a profile, or log
     * it. */
    struct UmhForwardInput : TerminalInput {
        bool lkm_loaded = false;

        UmhLkmSource lkm_source = UmhLkmSource::None;
        std::array<char, kUmhKmiLabelBytes> kmi_label{};
        /* Vendor carrier file the endgame patched (empty when none resolved). */
        std::array<char, kUmhCarrierPathBytes> carrier_path{};
        /* Kernel-side late-load command (argv + SELinux exec context). */
        UmhCommand command{};

        const void *session_secrets = nullptr;
        std::size_t session_secrets_size = 0U;
        /* Forward/wait handle copied from the backend's injected deps. A null
         * handle (the default) makes UmhForwardPolicy::run fail closed. */
        UmhForwardChannel channel{};

        void set_kmi_label(std::string_view text) noexcept {
            copy_bounded(kmi_label, text);
        }

        void set_carrier_path(std::string_view text) noexcept {
            copy_bounded(carrier_path, text);
        }

    private:
        template <std::size_t N>
        static void copy_bounded(std::array<char, N> &buffer,
                                 std::string_view text) noexcept {
            const std::size_t n =
                    text.size() < N - 1U ? text.size() : N - 1U;
            for (std::size_t i = 0U; i < n; ++i) {
                buffer[i] = text[i];
            }
            buffer[n] = '\0';
        }
    };
} // namespace ghostlock::terminal

#endif
