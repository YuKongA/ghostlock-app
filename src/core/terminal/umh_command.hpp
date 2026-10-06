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
    /* Bounded argv capacity (one slot is always reserved for the kernel's NULL
     * terminator). 8 was enough for {program, late-load, [--package-name X],
     * [--ro-partitions], [--soft-reboot]}; the 43284/43499 behavior alignment
     * (ruling 2026-10-05) adds the verified pair "--kmi <label> --allow-shell",
     * so the worst case is 9 args + NULL. The bound moved to 12 instead of 10 to
     * keep headroom for one more flag pair without touching the contract again. */
    inline constexpr std::size_t kUmhMaxArgc = 12U;
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
