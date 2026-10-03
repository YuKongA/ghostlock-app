#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_POLICY_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_POLICY_HPP

/* CVE-2026-43284 LKM selection and KMI policy (B5-4).
 *
 * The kernel module itself stays out-of-tree (an external, rebuilt-and-audited
 * asset); this module only validates device facts and resolves profile tokens.
 * Mapped from third_party/dirtyfrag/lkm/{ankit,dfroot,dfreroot}/dirtyfrag.c and
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
    };

    inline constexpr std::size_t kSupportedKmiCount = 8U;

    /* The eight KMIs the DirtyFrag build.sh / select_ko_image tables support
     * (third_party/dirtyfrag/lkm/ankit/build.sh). Order matches upstream. */
    inline constexpr std::array<SupportedKmi, kSupportedKmiCount> kSupportedKmis = {{
        {12U, 5U, 10U, 5010U, "android12-5.10"},
        {13U, 5U, 10U, 5010U, "android13-5.10"},
        {13U, 5U, 15U, 5015U, "android13-5.15"},
        {14U, 5U, 15U, 5015U, "android14-5.15"},
        {14U, 6U, 1U, 6001U, "android14-6.1"},
        {15U, 6U, 6U, 6006U, "android15-6.6"},
        {16U, 6U, 12U, 6012U, "android16-6.12"},
        {17U, 6U, 18U, 6018U, "android17-6.18"},
    }};

    /* Parse either a canonical KMI label (android13-5.15) or a uname -r
     * release (5.15.202-android13-8-g...). Returns false when the string is
     * not a well-formed release; it does not consult the supported table. */
    [[nodiscard]] bool parse_kernel_release(std::string_view release,
                                            KernelRelease &out) noexcept;

    /* Exact (android_release, kmi) lookup; nullptr when unsupported. Never
     * guesses across android releases that share a kmi. */
    [[nodiscard]] const SupportedKmi *find_supported_kmi(
            std::uint8_t android_release, std::uint16_t kmi) noexcept;

    enum class LkmSource : std::uint8_t { BundledKmi = 0, CustomFile = 1 };

    /* lkm_path token vocabulary (GLK u64 cannot carry a literal path). */
    inline constexpr std::uint64_t kLkmPathTokenBundled = 0U;
    inline constexpr std::uint64_t kLkmPathTokenCustomFile = 1U;

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
     * platform never includes a 43284 header (ADR-0004 R1). */
    struct DeviceKernelFacts final {
        std::string_view release{}; /* uname -r */
        bool has_f4c50a4 = false;   /* true = already patched, not applicable */
    };

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

    /* Policy inputs; optional mirrors the GLK field presence semantics. */
    struct LkmPolicyInput final {
        DeviceKernelFacts facts{};
        std::optional<std::uint16_t> profile_kmi{};
        std::optional<std::uint64_t> lkm_path_token{};
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
