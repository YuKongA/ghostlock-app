#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_IMAGE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_IMAGE_HPP

/* CVE-2026-43284 loaded-module precheck and UMH command (B5-4), extended with
 * the B5-9h-3 full vermagic check and in-place vermagic rewrite.
 *
 * The .ko is an external, rebuilt-and-audited asset and is never embedded in
 * this repository. This module only (a) prechecks the asset's ELF/.modinfo
 * facts host-side without loading it, (b) builds the late-load argv the
 * kernel-side helper will run and (c) can reconcile the module's vermagic with
 * the running kernel's required value. Real finit_module/insmod and the actual
 * call_usermodehelper path are device steps (B5-9).
 *
 * The precheck mirrors third_party/dirtyfrag/usermode/ankit/exp.c
 * read_custom_ko plus the design 4.1 facts: full vermagic equality, empty
 * __versions, unsigned. Fail-closed: anything unverifiable is rejected.
 *
 * B5-9h-3 constructs the required value from the running kernel's
 * VERMAGIC_STRING (include/linux/vermagic.h and arch/arm64/include/asm/
 * vermagic.h):
 *     UTS_RELEASE " " [SMP ] [preempt ] [mod_unload ] [modversions ]aarch64
 * The target rule maps mod_unload to CONFIG_MODULE_FORCE_UNLOAD being unset
 * and modversions to CONFIG_MODVERSIONS; every argument is explicit in
 * DeviceKernelFacts so no field is guessed here. Kernel 5.15's check_modinfo()
 * calls same_magic() and compares the strings in full when the module carries
 * no __versions CRCs, which is exactly the module this path validates.
 *
 * backend -> terminal is an allowed edge (ADR-0004 R1); pipeline/ is not. */

#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "terminal/root_program.hpp"
#include "terminal/umh_command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::lkm {

    /* The argv surface is terminal-neutral (R1/R19): the type and its bounds
     * live in terminal/umh_command.hpp and the backend names them here so the
     * lkm policy keeps its existing vocabulary. */
    using UmhCommand = terminal::UmhCommand;
    inline constexpr std::size_t kUmhMaxArgc = terminal::kUmhMaxArgc;
    inline constexpr std::size_t kUmhArgBytes = terminal::kUmhArgBytes;
    inline constexpr std::size_t kModuleMinBytes = 64U;
    inline constexpr std::size_t kModuleMaxBytes = std::size_t{64U} * 1024U * 1024U;

    /* Required and observed vermagic strings are bounded well above UTS_RELEASE
     * (64) plus the longest flag set; the comparison itself always uses the
     * full parsed string, never the truncated diagnostic copy. */
    inline constexpr std::size_t kVermagicMaxBytes = 192U;

    enum class UmhCommandError : std::uint8_t {
        None = 0,
        MissingRootProgram,
        MissingPackageName,
        UnknownLateLoadArgs,
        UnknownSelinuxContext,
        ArgTooLong,
        ArgInvalid,
        TooManyArgs,
    };

    /* Package name the KernelSU late-load variant must pass; empty when this
     * implementation has no validated name for the kind (never guessed). */
    [[nodiscard]] std::string_view default_root_package(
            terminal::RootProgramKind kind) noexcept;

    /* Build ksud-style argv: {program, "late-load", flags...}. No shell is
     * involved and no string is concatenated, so shell metacharacters are
     * inert; arguments are length-bounded and control-byte checked. */
    [[nodiscard]] bool build_late_load_command(const terminal::RootProgram &root_program,
                                               std::string_view package_name,
                                               std::uint32_t late_load_args,
                                               std::uint32_t selinux_exec_context,
                                               UmhCommand &out,
                                               UmhCommandError &error) noexcept;

    enum class LkmImageError : std::uint8_t {
        None = 0,
        ReadFailed,
        NotRegular,
        TooSmall,
        TooLarge,
        NotElf,
        NotAarch64,
        MissingModinfo,
        MissingName,
        VermagicMissing,
        VermagicMismatch,
        /* The original vermagic= slot cannot hold the required string. */
        VermagicSlotTooSmall,
        NonEmptyVersions,
        SignedModule,
    };

    /* Why a full-string comparison failed; informational, the reject is the
     * mismatch itself. Release means the UTS_RELEASE token differs, Options
     * means only the SMP/preempt/mod_unload/modversions/aarch64 tail differs,
     * Unparsable means one side carried no release token. */
    enum class VermagicDiffReason : std::uint8_t {
        None = 0,
        Release,
        Options,
        Unparsable,
    };
    [[nodiscard]] std::string_view vermagic_diff_reason_name(
            VermagicDiffReason reason) noexcept;

    /* Reconciliation outcome carried into the staged diagnostics. */
    enum class VermagicOutcome : std::uint8_t {
        Unchecked = 0, /* no precheck ran, or it failed for a non-vermagic rule */
        Original,      /* precheck passed with the original vermagic */
        Required,      /* mismatch and the rewrite policy disallowed the fix */
        Rewritten,     /* rewritten in place and re-verified */
    };
    [[nodiscard]] std::string_view vermagic_outcome_name(
            VermagicOutcome outcome) noexcept;

    /* Facts observed by the precheck; informational flags (kcfi_present) never
     * decide pass/fail because the target CFI scheme is a device fact. The two
     * text copies and the diff reason are diagnostics only; the decision uses
     * the full strings. */
    struct ModuleFacts final {
        bool elf_valid = false;
        bool has_modinfo = false;
        bool has_name = false;
        bool has_vermagic = false;
        bool vermagic_matches = false;
        bool vermagic_rewritten = false;
        bool versions_empty = true;
        bool signed_module = false;
        bool kcfi_present = false;
        VermagicDiffReason vermagic_diff = VermagicDiffReason::None;
        char module_vermagic[kVermagicMaxBytes] = {};
        char required_vermagic[kVermagicMaxBytes] = {};
    };

    /* First release token of a uname -r string or a /proc/version line: for
     * "Linux version <token> ..." the token after the prefix, otherwise the
     * first whitespace-delimited token. Empty when there is none. */
    [[nodiscard]] std::string_view uts_release_token(std::string_view version_text) noexcept;

    /* Build the kernel-required vermagic (NUL-terminated) and its length from
     * DeviceKernelFacts. Fails closed with out[0] == 0 when the release token
     * is empty or the result does not fit capacity. */
    [[nodiscard]] bool required_vermagic(const DeviceKernelFacts &facts, char *out,
                                         std::size_t capacity,
                                         std::size_t &length) noexcept;

    /* Precheck an in-memory .ko against the kernel-required vermagic. Never
     * loads or executes anything. Returns false with the first failing rule. */
    [[nodiscard]] bool precheck_module_bytes(const std::uint8_t *data, std::size_t size,
                                             const DeviceKernelFacts &required,
                                             ModuleFacts &facts,
                                             LkmImageError &error) noexcept;

    /* Read a .ko path (existence/regular-file/size), then precheck its bytes. */
    [[nodiscard]] bool precheck_module_file(std::string_view path,
                                            const DeviceKernelFacts &required,
                                            ModuleFacts &facts,
                                            LkmImageError &error) noexcept;

    /* Rewrite the .modinfo "vermagic=" entry of an in-memory .ko in place to
     * required, NUL-padded. The .modinfo section length, every other entry and
     * every other byte of the image are left unchanged. Fails closed (image
     * byte-identical) when the image is not a parseable ELF64 relocatable
     * aarch64 module with a vermagic= entry, when required is empty, or when
     * required does not fit the original slot ("vermagic=" + old value + NUL). */
    [[nodiscard]] bool rewrite_vermagic(std::uint8_t *image, std::size_t size,
                                        std::string_view required,
                                        LkmImageError &error) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::lkm

#endif
