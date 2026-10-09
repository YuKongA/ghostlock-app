/** @file profile/payload_keys.cpp
 *  Building one payload key path, bounded and only for admissible parts.
 */
#include "profile/payload_keys.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::string_view kPrefix = "backend.";
        constexpr std::string_view kSegment = ".payload.";
        /** Bigger than any admissible segment: a backend token, a terminal and
         *  a bounded suffix, plus the separators. */
        constexpr std::size_t kSegmentCapacity = 192U;

        void append(char *out, std::size_t &at, std::string_view text) noexcept {
            for (const char c : text) {
                out[at] = c;
                ++at;
            }
        }
    }  // namespace

    bool payload_segment(char *out, std::size_t capacity, std::string_view backend,
                         std::string_view name) noexcept {
        if (out == nullptr || capacity == 0U) {
            return false;
        }
        out[0] = '\0';
        if (backend.empty() || !payload_name_is_valid(name)) {
            return false;
        }
        const std::size_t needed = kPrefix.size() + backend.size() + kSegment.size() +
                                   name.size() + 1U;
        if (needed > capacity) {
            return false;
        }
        std::size_t at = 0U;
        append(out, at, kPrefix);
        append(out, at, backend);
        append(out, at, kSegment);
        append(out, at, name);
        out[at] = '\0';
        return true;
    }

    bool payload_path(char *out, std::size_t capacity, std::string_view backend,
                      std::string_view name, std::string_view key) noexcept {
        if (out == nullptr || capacity == 0U) {
            return false;
        }
        out[0] = '\0';
        if (!payload_key_is_declared(key)) {
            return false;
        }
        char segment[kSegmentCapacity];
        if (!payload_segment(segment, sizeof(segment), backend, name)) {
            return false;
        }
        const std::string_view segment_view(segment);
        const std::size_t needed = segment_view.size() + 1U + key.size() + 1U;
        if (needed > capacity) {
            return false;
        }
        std::size_t at = 0U;
        append(out, at, segment_view);
        out[at] = '.';
        ++at;
        append(out, at, key);
        out[at] = '\0';
        return true;
    }
}  // namespace ghostlock::profile
