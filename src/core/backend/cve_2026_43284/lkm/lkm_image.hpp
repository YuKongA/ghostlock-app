#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_IMAGE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_LKM_LKM_IMAGE_HPP

/* CVE-2026-43284 loaded-module precheck and UMH command (B5-4).
 *
 * The .ko is an external, rebuilt-and-audited asset and is never embedded in
 * this repository. This module only (a) prechecks the asset's ELF/.modinfo
 * facts host-side without loading it and (b) builds the late-load argv the
 * kernel-side helper will run. Real finit_module/insmod and the actual
 * call_usermodehelper path are device steps (B5-9).
 *
 * The precheck mirrors third_party/dirtyfrag/usermode/ankit/exp.c
 * read_custom_ko plus the design 4.1 facts: vermagic fragment, empty
 * __versions, unsigned. Fail-closed: anything unverifiable is rejected.
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
        NonEmptyVersions,
        SignedModule,
    };

    /* Facts observed by the precheck; informational flags (kcfi_present) never
     * decide pass/fail because the target CFI scheme is a device fact. */
    struct ModuleFacts final {
        bool elf_valid = false;
        bool has_modinfo = false;
        bool has_name = false;
        bool has_vermagic = false;
        bool vermagic_matches = false;
        bool versions_empty = true;
        bool signed_module = false;
        bool kcfi_present = false;
    };

    /* Precheck an in-memory .ko against the target kernel release. Never loads
     * or executes anything. Returns false with the first failing rule. */
    [[nodiscard]] bool precheck_module_bytes(const std::uint8_t *data, std::size_t size,
                                             const KernelRelease &release, ModuleFacts &facts,
                                             LkmImageError &error) noexcept;

    /* Read a .ko path (existence/regular-file/size), then precheck its bytes. */
    [[nodiscard]] bool precheck_module_file(std::string_view path,
                                            const KernelRelease &release, ModuleFacts &facts,
                                            LkmImageError &error) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::lkm

#endif
