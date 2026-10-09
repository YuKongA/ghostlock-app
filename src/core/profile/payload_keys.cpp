/** @file profile/payload_keys.cpp
 *  Building one payload key path, bounded and only for admissible parts.
 */
#include "profile/payload_keys.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::string_view kPrefix = "backend.";
        constexpr std::string_view kSegment = ".payload.";

        void append(char *out, std::size_t &at, std::string_view text) noexcept {
            for (const char c : text) {
                out[at] = c;
                ++at;
            }
        }
    }  // namespace

    bool payload_path(char *out, std::size_t capacity, std::string_view backend,
                      std::string_view name, std::string_view key) noexcept {
        if (out == nullptr || capacity == 0U) {
            return false;
        }
        out[0] = '\0';
        if (backend.empty() || !payload_name_is_valid(name) ||
            !payload_key_is_declared(key)) {
            return false;
        }
        const std::size_t needed =
            kPrefix.size() + backend.size() + kSegment.size() + name.size() + 1U + key.size() + 1U;
        if (needed > capacity) {
            return false;
        }
        std::size_t at = 0U;
        append(out, at, kPrefix);
        append(out, at, backend);
        append(out, at, kSegment);
        append(out, at, name);
        out[at] = '.';
        ++at;
        append(out, at, key);
        out[at] = '\0';
        return true;
    }
}  // namespace ghostlock::profile
