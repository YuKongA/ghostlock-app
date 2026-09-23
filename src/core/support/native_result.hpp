#ifndef GHOSTLOCK_NATIVE_RESULT_HPP
#define GHOSTLOCK_NATIVE_RESULT_HPP

#include <cerrno>
#include <expected>
#include <system_error>

namespace ghostlock::support {
    struct SysError final {
        std::error_code code;

        [[nodiscard]] static SysError from_errno(int32_t value = errno) noexcept {
            return {{value, std::generic_category()}};
        }
    };

    /* C++23 expected, keeping the old Result name: has_value()/value()/error()
     * are unchanged, construction uses std::unexpected for failures. */
    template<typename T, typename E = SysError>
    using Result = std::expected<T, E>;
} // namespace ghostlock::support

#endif
