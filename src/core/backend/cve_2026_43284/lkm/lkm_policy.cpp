/* CVE-2026-43284 LKM selection and KMI policy (B5-4) -- implementation.
 *
 * Pure host-testable logic: string parsing and table lookup only. It never
 * probes the device, never touches a module and never includes pipeline/. */

#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"

namespace ghostlock::backend::cve_2026_43284::lkm {
    namespace {
        constexpr std::string_view kAndroidSuffix = "android";

        /* Parse 1..n ASCII digits at text[pos]; rejects overflow past max. */
        bool parse_u32(std::string_view text, std::size_t &pos, std::uint32_t &out,
                       std::uint32_t max) noexcept {
            const std::size_t begin = pos;
            std::uint32_t value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                const std::uint32_t digit = static_cast<std::uint32_t>(text[pos] - '0');
                if (value > (max - digit) / 10U) {
                    return false;
                }
                value = value * 10U + digit;
                ++pos;
            }
            if (pos == begin) {
                return false;
            }
            out = value;
            return true;
        }

        bool parse_major_minor(std::string_view text, std::size_t pos, std::uint16_t &major,
                               std::uint16_t &minor) noexcept {
            std::uint32_t maj = 0;
            std::uint32_t min = 0;
            if (!parse_u32(text, pos, maj, 65535U)) {
                return false;
            }
            if (pos >= text.size() || text[pos] != '.') {
                return false;
            }
            ++pos;
            if (!parse_u32(text, pos, min, 65535U)) {
                return false;
            }
            major = static_cast<std::uint16_t>(maj);
            minor = static_cast<std::uint16_t>(min);
            return true;
        }
    } // namespace

    bool parse_kernel_release(std::string_view release, KernelRelease &out) noexcept {
        KernelRelease parsed{};
        const std::size_t android_pos = release.find(kAndroidSuffix);
        if (android_pos == std::string_view::npos) {
            /* No androidN suffix: the kernel version can still be parsed so the
             * caller can report a precise failure, but it never matches a row. */
            std::size_t pos = 0;
            if (!parse_major_minor(release, pos, parsed.kernel_major, parsed.kernel_minor)) {
                return false;
            }
        } else {
            std::size_t pos = android_pos + kAndroidSuffix.size();
            std::uint32_t android = 0;
            if (!parse_u32(release, pos, android, 255U)) {
                return false;
            }
            parsed.android_release = static_cast<std::uint8_t>(android);
            std::size_t kernel_pos = 0;
            if (android_pos == 0U) {
                /* Canonical label: android<X>-<major>.<minor>. */
                if (pos >= release.size() || release[pos] != '-') {
                    return false;
                }
                kernel_pos = pos + 1U;
            } else {
                /* uname form: <major>.<minor>.<patch>-android<X>-...; the
                 * leading token before the first '-' is the kernel version. */
                kernel_pos = 0U;
            }
            if (!parse_major_minor(release, kernel_pos, parsed.kernel_major,
                                   parsed.kernel_minor)) {
                return false;
            }
        }
        const std::uint32_t kmi = static_cast<std::uint32_t>(parsed.kernel_major) * 1000U +
                                  static_cast<std::uint32_t>(parsed.kernel_minor);
        if (kmi > 65535U) {
            return false;
        }
        parsed.kmi = static_cast<std::uint16_t>(kmi);
        out = parsed;
        return true;
    }

    const SupportedKmi *find_supported_kmi(std::uint8_t android_release,
                                           std::uint16_t kmi) noexcept {
        for (const SupportedKmi &entry : kSupportedKmis) {
            if (entry.android_release == android_release && entry.kmi == kmi) {
                return &entry;
            }
        }
        return nullptr;
    }

    bool resolve_lkm_selection(const LkmPolicyInput &input, LkmSelection &out,
                               LkmPolicyError &error) noexcept {
        error = LkmPolicyError::None;
        if (input.facts.release.empty()) {
            error = LkmPolicyError::MissingRelease;
            return false;
        }
        KernelRelease release{};
        if (!parse_kernel_release(input.facts.release, release)) {
            error = LkmPolicyError::ReleaseUnparsable;
            return false;
        }
        if (input.facts.has_f4c50a4) {
            error = LkmPolicyError::PatchedKernel;
            return false;
        }
        if (!input.profile_kmi.has_value()) {
            error = LkmPolicyError::MissingProfileKmi;
            return false;
        }
        if (input.profile_kmi.value() != release.kmi) {
            error = LkmPolicyError::KmiFieldMismatch;
            return false;
        }
        const SupportedKmi *kmi = find_supported_kmi(release.android_release, release.kmi);
        if (kmi == nullptr) {
            error = LkmPolicyError::UnsupportedKmi;
            return false;
        }
        if (!input.lkm_path_token.has_value()) {
            error = LkmPolicyError::MissingLkmPath;
            return false;
        }
        LkmSource source = LkmSource::BundledKmi;
        switch (input.lkm_path_token.value()) {
        case kLkmPathTokenBundled:
            source = LkmSource::BundledKmi;
            break;
        case kLkmPathTokenCustomFile:
            source = LkmSource::CustomFile;
            break;
        default:
            error = LkmPolicyError::UnknownLkmSource;
            return false;
        }
        std::uint32_t late_load_args = 0;
        if (input.late_load_args_token.has_value()) {
            const std::uint64_t token = input.late_load_args_token.value();
            if ((token & ~static_cast<std::uint64_t>(kLateLoadArgsKnown)) != 0U) {
                error = LkmPolicyError::UnknownLateLoadArgs;
                return false;
            }
            late_load_args = static_cast<std::uint32_t>(token);
        }
        LkmSelection resolved{};
        resolved.source = source;
        resolved.release = release;
        resolved.kmi = kmi;
        resolved.late_load_args = late_load_args;
        out = resolved;
        return true;
    }
} // namespace ghostlock::backend::cve_2026_43284::lkm
