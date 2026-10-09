#pragma once
/** @file contract/kmi_catalog.hpp
 *  The KMI labels a module can be built for, and which one a release wants.
 *
 *  A label is a delivery identity: the kernel interface version plus the Android
 *  generation the module was built against. Two labels can share one interface
 *  version -- android12 and android13 share 5.10 -- because the label names the
 *  file that must be delivered, not the version it speaks. The list is the native
 *  side's, and the other end reads an export of it rather than keeping its own.
 *
 *  The derivation is the rule the other end already applies to a release string:
 *  the first major.minor, the -androidN token, and the two joined. A release that
 *  carries neither is not a release this can deliver a module for, and says so.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::contract {
    /** One label, with the two numbers it is made of. */
    struct KmiEntry final {
        const char *label;
        /** The Android generation the module was built for. */
        std::uint32_t android;
        /** The kernel interface version, as hundreds: 5.15 is 5015. */
        std::uint32_t kmi;
    };

    /** Every label this build can deliver. */
    inline constexpr KmiEntry kKmiCatalog[] = {
        {"android12-5.10", 12U, 5010U},
        {"android13-5.10", 13U, 5010U},
        {"android13-5.15", 13U, 5015U},
        {"android14-5.15", 14U, 5015U},
        {"android14-6.1", 14U, 6001U},
        {"android15-6.6", 15U, 6006U},
        {"android16-6.12", 16U, 6012U},
        {"android17-6.18", 17U, 6018U},
    };
    inline constexpr std::size_t kKmiCount = sizeof(kKmiCatalog) / sizeof(kKmiCatalog[0]);

    /** The two numbers a release string carries, or nothing when it carries
     *  neither. Written out rather than matched with a library: the rule is two
     *  scans, and the tree keeps its dependencies small on purpose. */
    struct ReleaseParts final {
        std::uint32_t major = 0U;
        std::uint32_t minor = 0U;
        std::uint32_t android = 0U;
        bool has_kernel = false;
        bool has_android = false;
    };

    [[nodiscard]] constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

    [[nodiscard]] constexpr ReleaseParts release_parts(const char *release) noexcept {
        ReleaseParts parts{};
        if (release == nullptr) {
            return parts;
        }
        /* The first major.minor followed by a dot: 5.15.189 has it at the start. */
        for (std::size_t i = 0U; release[i] != '\0'; ++i) {
            if (!digit(release[i])) {
                continue;
            }
            std::uint32_t major = 0U;
            std::size_t j = i;
            while (digit(release[j])) {
                major = major * 10U + static_cast<std::uint32_t>(release[j] - '0');
                ++j;
            }
            if (release[j] != '.') {
                continue;
            }
            std::uint32_t minor = 0U;
            std::size_t k = j + 1U;
            while (digit(release[k])) {
                minor = minor * 10U + static_cast<std::uint32_t>(release[k] - '0');
                ++k;
            }
            if (k == j + 1U || release[k] != '.') {
                continue;
            }
            parts.major = major;
            parts.minor = minor;
            parts.has_kernel = true;
            break;
        }
        /* The -androidN token, which is the generation the module must match. */
        for (std::size_t i = 0U; release[i] != '\0'; ++i) {
            const char *marker = "-android";
            std::size_t m = 0U;
            while (marker[m] != '\0' && release[i + m] == marker[m]) {
                ++m;
            }
            if (marker[m] != '\0') {
                continue;
            }
            std::size_t j = i + m;
            if (!digit(release[j])) {
                continue;
            }
            std::uint32_t android = 0U;
            while (digit(release[j])) {
                android = android * 10U + static_cast<std::uint32_t>(release[j] - '0');
                ++j;
            }
            parts.android = android;
            parts.has_android = true;
            break;
        }
        return parts;
    }

    /** Write the label a release wants into out; false when it wants none. */
    [[nodiscard]] bool label_for_release(const char *release, char *out,

                                        std::size_t capacity) noexcept;

    /** The catalogue row a label names, or nullptr. */
    [[nodiscard]] constexpr const KmiEntry *kmi_of(std::string_view label) noexcept {
        for (std::size_t i = 0U; i < kKmiCount; ++i) {
            const std::string_view declared(kKmiCatalog[i].label);
            if (declared.size() == label.size() && declared == label) {
                return &kKmiCatalog[i];
            }
        }
        return nullptr;
    }
}  // namespace ghostlock::contract
