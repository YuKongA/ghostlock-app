/** @file contract/kmi_catalog.cpp
 *  Joining the two numbers a release carries into the label it wants.
 */
#include "contract/kmi_catalog.hpp"

namespace ghostlock::contract {
    bool label_for_release(const char *release, char *out, std::size_t capacity) noexcept {
        if (out == nullptr || capacity == 0U) {
            return false;
        }
        out[0] = '\0';
        const ReleaseParts parts = release_parts(release);
        if (!parts.has_kernel || !parts.has_android) {
            return false;
        }
        /* android<N>-<major>.<minor>, written by hand for the same reason the
         * parse is: no formatting library on a path that runs inside the window. */
        char built[64];
        std::size_t at = 0U;
        const auto put = [&built, &at](std::string_view text) noexcept {
            for (const char c : text) {
                if (at + 1U < sizeof(built)) {
                    built[at] = c;
                    ++at;
                }
            }
        };
        const auto put_number = [&built, &at](std::uint32_t value) noexcept {
            char digits[12];
            std::size_t count = 0U;
            if (value == 0U) {
                digits[count++] = '0';
            }
            while (value != 0U) {
                digits[count++] = static_cast<char>('0' + (value % 10U));
                value /= 10U;
            }
            while (count != 0U && at + 1U < sizeof(built)) {
                built[at] = digits[count - 1U];
                ++at;
                --count;
            }
        };
        put(std::string_view("android"));
        put_number(parts.android);
        put(std::string_view("-"));
        put_number(parts.major);
        put(std::string_view("."));
        put_number(parts.minor);
        built[at] = '\0';
        if (at + 1U > capacity) {
            return false;
        }
        for (std::size_t i = 0U; i <= at; ++i) {
            out[i] = built[i];
        }
        return true;
    }
}  // namespace ghostlock::contract
