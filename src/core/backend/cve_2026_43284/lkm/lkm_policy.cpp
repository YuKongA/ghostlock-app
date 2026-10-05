/* CVE-2026-43284 LKM selection and KMI policy (B5-4) -- implementation.
 *
 * Pure host-testable logic: string parsing and table lookup only. It never
 * probes the device, never touches a module and never includes pipeline/. */

#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"

namespace ghostlock::backend::cve_2026_43284::lkm {
    bool proc_version_has_preempt(std::string_view proc_version) noexcept {
        return proc_version.find("PREEMPT") != std::string_view::npos;
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
        /* The KMI is a device fact derived from the running release, not policy:
         * the App-driven production path is not expected to repeat it, so an
         * absent token resolves to the derived value. An explicit token that
         * disagrees is still rejected (a profile written for another KMI must not
         * silently run). */
        const std::uint16_t kmi_token = input.profile_kmi.value_or(release.kmi);
        if (kmi_token != release.kmi) {
            error = LkmPolicyError::KmiFieldMismatch;
            return false;
        }
        const SupportedKmi *kmi = find_supported_kmi(release.android_release, release.kmi);
        if (kmi == nullptr) {
            error = LkmPolicyError::UnsupportedKmi;
            return false;
        }
        /* S4 R4: an explicit (non-empty) profile path selects custom-file
         * delivery; absence keeps the bundled $GHOSTLOCK_HOME/helper.ko mirror.
         * There is no unknown-token path any more (the wire carries text). */
        const LkmSource source =
                (input.lkm_path.has_value() && !input.lkm_path->empty())
                        ? LkmSource::CustomFile
                        : LkmSource::BundledKmi;
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
