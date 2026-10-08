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

    /* Read-only LKM/UMH readiness. The endgame chain -- our own LKM plus the
     * kernel-side call_usermodehelper it performs -- completes the module load;
     * the umh_forward terminal writes nothing, forwards nothing and execs
     * nothing. It only reads back the two device facts the decision names:
     * /dev/dfm0 exists and /proc/modules contains "kernelsu". */
    enum class UmhReadyState : std::uint8_t {
        Ready = 0,
        NotReady,
        Unavailable,
    };

    /* Read-only readiness probe bound by the composition root. Markers are
     * checked from the least privileged domain outward: the UMH script's
     * /data/local/tmp marker first (an app-domain process may stat it), then the
     * upstream /dev/dfm0 + /proc/modules pair. NotReady means the terminus was
     * observed to be incomplete; Unavailable means this domain cannot observe it
     * at all, which the terminal treats as a degraded success because the backend
     * already proved lkm_loaded. Performs no write. */
    using UmhReadyFn = UmhReadyState (*)(void *ctx) noexcept;

    /* Neutral UMH readiness handle (ADR-0004 R19): the backend copies the
     * handle it was handed into UmhForwardInput after a clean terminus; the
     * umh_forward terminal performs the read-only probe. ctx is optional (the
     * production probe ignores it); a null ready fn fails closed, so an
     * unbound channel can never be treated as success. Function pointers keep
     * the handle host-testable without a device. */
    struct UmhForwardChannel final {
        void *ctx = nullptr;
        UmhReadyFn ready = nullptr;

        [[nodiscard]] bool valid() const noexcept { return ready != nullptr; }
    };

    /* Input for the umh_forward terminal: the backend reports the UMH/LKM state
     * and the read-only readiness handle; the terminal only confirms readiness.
     *
     * B5-7/B6-T5 backend handoff. The cve_2026_43284 backend fills every field
     * after its endgame terminus; the terminal only reads them. session_secrets
     * is an opaque, non-owning reference to the backend-owned IpsecSaParams: the
     * composition root injects it and the chain consumes it, valid through
     * Terminal::run and zeroized by the backend state destructor. The terminal
     * must never persist, copy to a profile, forward or log it. */
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
