#ifndef GHOSTLOCK_TERMINAL_UMH_COMMAND_HPP
#define GHOSTLOCK_TERMINAL_UMH_COMMAND_HPP

/* Neutral UMH argv spec (ADR-0004 R1/R19). The cve_2026_43284 backend builds
 * it (lkm::build_late_load_command) and the future umh_forward terminal
 * consumes it; it lives in terminal/ so the terminal never includes a backend
 * header. Fixed-capacity, NUL-terminated, trivially copyable and host-safe. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::terminal {
    inline constexpr std::size_t kUmhMaxArgc = 8U;
    inline constexpr std::size_t kUmhArgBytes = 96U;

    struct UmhCommand final {
        /* NUL-terminated argv; one spare slot is always kept for the kernel's
         * NULL terminator. */
        std::array<std::array<char, kUmhArgBytes>, kUmhMaxArgc> argv{};
        std::size_t argc = 0U;
        /* SELinux exec context token selected for the transition. */
        std::uint32_t selinux_exec_context = 0U;

        [[nodiscard]] std::string_view arg(std::size_t index) const noexcept {
            if (index >= argc) {
                return {};
            }
            return std::string_view(argv[index].data());
        }
    };
} // namespace ghostlock::terminal

#endif
