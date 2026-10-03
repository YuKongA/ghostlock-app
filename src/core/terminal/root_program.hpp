#ifndef GHOSTLOCK_TERMINAL_ROOT_PROGRAM_HPP
#define GHOSTLOCK_TERMINAL_ROOT_PROGRAM_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::terminal {
    /* Which root program the App selected for this session (single value). The
     * program is a parameter, never a compile-time binding; both the root_child
     * and the umh_forward terminal can launch it. */
    enum class RootProgramKind : std::uint8_t {
        KernelSU = 0,
        FolkPatch = 1,
        Custom = 2,
    };

    /* Neutral, host-safe spec: kind plus a bounded argv string (program path and
     * arguments as the launcher receives them). No heap, trivially copyable. */
    struct RootProgram final {
        static constexpr std::size_t kArgvCapacity = 192;

        RootProgramKind kind = RootProgramKind::KernelSU;
        std::array<char, kArgvCapacity> argv{};

        /* Bounded copy; always NUL-terminates, truncates rather than overflows. */
        void set_argv(std::string_view text) noexcept {
            const std::size_t n = text.size() < kArgvCapacity - 1 ? text.size() : kArgvCapacity - 1;
            for (std::size_t i = 0; i < n; ++i) argv[i] = text[i];
            argv[n] = '\0';
        }

        [[nodiscard]] std::string_view argv_view() const noexcept {
            return std::string_view(argv.data());
        }
    };
} // namespace ghostlock::terminal

#endif
