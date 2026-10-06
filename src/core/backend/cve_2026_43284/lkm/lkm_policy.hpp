#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_POLICY_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_POLICY_HPP

/* CVE-2026-43284 LKM selection and KMI policy (B5-4).
 *
 * The kernel module itself stays out-of-tree (an external, rebuilt-and-audited
 * asset); this module only validates device facts and resolves profile tokens.
 * Mapped from DirtyFrag-Android-Root-Jailbreak@de2ab7b lkm/ankit/dirtyfrag.c,
 * DFRoot@3050d5b lkm/, DFReroot@9edc769 lkm/ and
 * usermode/ankit/exp.c (select_ko_image, read_device_versions).
 *
 * Fail-closed contract: an unparsable release, a missing/mismatched profile
 * kmi, a KMI outside the eight supported entries, an unknown token or an
 * already-patched kernel (f4c50a4) never selects an image and never falls back
 * to a guess. The backend never probes the device itself (ADR-0004 R1); the
 * platform supplies DeviceKernelFacts.
 *
 * This header must not include pipeline/ (ADR-0004 R1). */

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::lkm {

    /* Parsed Android kernel identity. android_release == 0 means the release
     * string carried no androidN suffix and therefore cannot be matched
     * against the supported table (no guess, no fallback). */
    struct KernelRelease final {
        std::uint8_t android_release = 0;
        std::uint16_t kernel_major = 0;
        std::uint16_t kernel_minor = 0;
        std::uint16_t kmi = 0; /* kernel_major * 1000 + kernel_minor */
    };

    /* One row of the supported KMI table. The pair (android_release, kmi) is
     * the key: android12-5.10 and android13-5.10 share kmi 5010 but are
     * distinct delivery targets. */
    struct SupportedKmi final {
        std::uint8_t android_release;
        std::uint16_t kernel_major;
        std::uint16_t kernel_minor;
        std::uint16_t kmi;
        std::string_view label; /* canonical androidX-major.minor */
        /* Build/delivery artifact name for this row (minimal-lkm-plan.md
         * section 2: "ghostlock-android13-5.15.ko"). The Gradle task builds one
         * image per row under this name and the exported lkm-kmi-manifest.tsv
         * carries it, so no consumer re-derives the convention. */
        std::string_view ko_filename;
    };

    inline constexpr std::size_t kSupportedKmiCount = 8U;

    /* The eight KMIs the DirtyFrag build.sh / select_ko_image tables support
     * (DirtyFrag-Android-Root-Jailbreak@de2ab7b lkm/ankit/build.sh). Order matches upstream. */
    inline constexpr std::array<SupportedKmi, kSupportedKmiCount> kSupportedKmis = {{
        {12U, 5U, 10U, 5010U, "android12-5.10", "ghostlock-android12-5.10.ko"},
        {13U, 5U, 10U, 5010U, "android13-5.10", "ghostlock-android13-5.10.ko"},
        {13U, 5U, 15U, 5015U, "android13-5.15", "ghostlock-android13-5.15.ko"},
        {14U, 5U, 15U, 5015U, "android14-5.15", "ghostlock-android14-5.15.ko"},
        {14U, 6U, 1U, 6001U, "android14-6.1", "ghostlock-android14-6.1.ko"},
        {15U, 6U, 6U, 6006U, "android15-6.6", "ghostlock-android15-6.6.ko"},
        {16U, 6U, 12U, 6012U, "android16-6.12", "ghostlock-android16-6.12.ko"},
        {17U, 6U, 18U, 6018U, "android17-6.18", "ghostlock-android17-6.18.ko"},
    }};

    namespace kmi_detail {
        inline constexpr std::string_view kAndroidSuffix = "android";

        /* Parse 1..n ASCII digits at text[pos]; rejects overflow past max. */
        inline bool parse_u32(std::string_view text, std::size_t &pos,
                              std::uint32_t &out, std::uint32_t max) noexcept {
            const std::size_t begin = pos;
            std::uint32_t value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                const std::uint32_t digit =
                        static_cast<std::uint32_t>(text[pos] - '0');
                if (value > (max - digit) / 10U) return false;
                value = value * 10U + digit;
                ++pos;
            }
            if (pos == begin) return false;
            out = value;
            return true;
        }

        inline bool parse_major_minor(std::string_view text, std::size_t pos,
                                      std::uint16_t &major,
                                      std::uint16_t &minor) noexcept {
            std::uint32_t maj = 0;
            std::uint32_t min = 0;
            if (!parse_u32(text, pos, maj, 65535U)) return false;
            if (pos >= text.size() || text[pos] != '.') return false;
            ++pos;
            if (!parse_u32(text, pos, min, 65535U)) return false;
            major = static_cast<std::uint16_t>(maj);
            minor = static_cast<std::uint16_t>(min);
            return true;
        }
    } // namespace kmi_detail

    /* Parse either a canonical KMI label (android13-5.15) or a uname -r
     * release (5.15.202-android13-8-g...). Header-inline (S4 R1) so a schema
     * default that derives the KMI does not force every including TU to link a
     * .cpp; still the single implementation used by resolve_lkm_selection. */
    [[nodiscard]] inline bool parse_kernel_release(std::string_view release,
                                                   KernelRelease &out) noexcept {
        KernelRelease parsed{};
        const std::size_t android_pos = release.find(kmi_detail::kAndroidSuffix);
        if (android_pos == std::string_view::npos) {
            std::size_t pos = 0;
            if (!kmi_detail::parse_major_minor(release, pos, parsed.kernel_major,
                                               parsed.kernel_minor)) {
                return false;
            }
        } else {
            std::size_t pos = android_pos + kmi_detail::kAndroidSuffix.size();
            std::uint32_t android = 0;
            if (!kmi_detail::parse_u32(release, pos, android, 255U)) return false;
            parsed.android_release = static_cast<std::uint8_t>(android);
            std::size_t kernel_pos = 0;
            if (android_pos == 0U) {
                if (pos >= release.size() || release[pos] != '-') return false;
                kernel_pos = pos + 1U;
            }
            if (!kmi_detail::parse_major_minor(release, kernel_pos,
                                               parsed.kernel_major,
                                               parsed.kernel_minor)) {
                return false;
            }
        }
        const std::uint32_t kmi =
                static_cast<std::uint32_t>(parsed.kernel_major) * 1000U +
                static_cast<std::uint32_t>(parsed.kernel_minor);
        if (kmi > 65535U) return false;
        parsed.kmi = static_cast<std::uint16_t>(kmi);
        out = parsed;
        return true;
    }

    /* Exact (android_release, kmi) lookup; nullptr when unsupported. Never
     * guesses across android releases that share a kmi. */
    [[nodiscard]] const SupportedKmi *find_supported_kmi(
            std::uint8_t android_release, std::uint16_t kmi) noexcept;

    enum class LkmSource : std::uint8_t { BundledKmi = 0, CustomFile = 1 };

    /* late_load_args policy bitmask (design 5.2). lkm_image turns the selected
     * bits into argv; unknown bits fail closed. */
    inline constexpr std::uint32_t kLateLoadArgPackageName = 1U << 0U;
    inline constexpr std::uint32_t kLateLoadArgRoPartitions = 1U << 1U;
    inline constexpr std::uint32_t kLateLoadArgSoftReboot = 1U << 2U;
    inline constexpr std::uint32_t kLateLoadArgsKnown =
            kLateLoadArgPackageName | kLateLoadArgRoPartitions | kLateLoadArgSoftReboot;

    /* selinux_exec_context token vocabulary (design 5.2). */
    inline constexpr std::uint32_t kSelinuxExecContextVendorModprobe = 0U;
    inline constexpr std::uint32_t kSelinuxExecContextInit = 1U;
    inline constexpr std::uint32_t kSelinuxExecContextCustom = 2U;
    inline constexpr std::uint32_t kSelinuxExecContextMax = kSelinuxExecContextCustom;

    /* Facts supplied by the platform; the backend never probes them itself and
     * platform never includes a 43284 header (ADR-0004 R1).
     *
     * B5-9h-3 adds the three module-build facts that complete the kernel's
     * VERMAGIC_STRING (include/linux/vermagic.h):
     *     UTS_RELEASE " " SMP [preempt ] [mod_unload ] [modversions ]aarch64
     * They are not derivable from an unprivileged userspace probe (there is no
     * readable /proc/config.gz), so the caller declares them. The defaults
     * encode the audited target GKI build (CONFIG_MODVERSIONS=y,
     * CONFIG_MODULE_FORCE_UNLOAD unset); a caller that cannot attest them must
     * leave the rewrite policy off, which keeps the wrong-vermagic path
     * fail-closed. */
    struct DeviceKernelFacts final {
        std::string_view release{}; /* uname -r / the release token of /proc/version */
        bool has_f4c50a4 = false;   /* true = already patched, not applicable */
        bool preempt = false;       /* CONFIG_PREEMPT -> "preempt" */
        bool module_force_unload = false; /* when unset, advertise "mod_unload" */
        bool modversions = true;    /* CONFIG_MODVERSIONS -> "modversions" */
    };

    /* True when a /proc/version line advertises a preemptible kernel. Pure so
     * the collector and the tests agree; PREEMPT_RT is intentionally not
     * modeled (the target kernel is plain PREEMPT). */
    [[nodiscard]] bool proc_version_has_preempt(std::string_view proc_version) noexcept;

    enum class LkmPolicyError : std::uint8_t {
        None = 0,
        MissingRelease,
        ReleaseUnparsable,
        PatchedKernel,
        MissingProfileKmi,
        KmiFieldMismatch,
        UnsupportedKmi,
        MissingLkmPath,
        UnknownLkmSource,
        UnknownLateLoadArgs,
    };

    /* The LkmPolicyError spelling authority for logs/reports is
     * diagnostic::lkm_policy_error_name() (diagnostic.hpp); the run log reuses
     * the same tokens through backend_terminal.cpp's local display mapper so the
     * attack path never grows a link dependency on the diagnostic CLI unit. */

    /* Policy inputs; optional mirrors the GLK field presence semantics. A
     * present (non-empty) lkm_path means the profile names its own .ko
     * (CustomFile); absent means the bundled $GHOSTLOCK_HOME/helper.ko mirror
     * (BundledKmi). The path is consumed by the composition root; this policy
     * only classifies the delivery source. */
    struct LkmPolicyInput final {
        DeviceKernelFacts facts{};
        std::optional<std::uint16_t> profile_kmi{};
        std::optional<std::string_view> lkm_path{};
        std::optional<std::uint64_t> late_load_args_token{};
    };

    struct LkmSelection final {
        LkmSource source = LkmSource::BundledKmi;
        KernelRelease release{};
        const SupportedKmi *kmi = nullptr; /* non-null once resolved */
        std::uint32_t late_load_args = 0;
    };

    /* Fail-closed resolution. On success out.kmi points into kSupportedKmis.
     * On failure out is untouched and error carries the first failing rule. */
    [[nodiscard]] bool resolve_lkm_selection(const LkmPolicyInput &input,
                                             LkmSelection &out,
                                             LkmPolicyError &error) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::lkm

#endif
