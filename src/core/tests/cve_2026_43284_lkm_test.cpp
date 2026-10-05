/* Host tests for B5-4: the CVE-2026-43284 LKM KMI policy, .ko precheck and UMH
 * late-load argv construction.
 *
 * No device, module or kernel dependency: the .ko fixtures are synthesized
 * ELF64 images with a real section-header table and a .modinfo string blob, so
 * the precheck exercises the same parsing the device path will use. Covered:
 * release parsing (uname and canonical forms), the eight-entry KMI table and
 * same-kmi/android-release disambiguation, release<->kmi consistency,
 * fail-closed error ordering, .modinfo positive/negative rules (vermagic,
 * __versions, unsigned, kCFI observation), file existence/regular/size
 * handling, and argv construction with length/field/injection boundaries. */

#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {
    using ghostlock::backend::cve_2026_43284::lkm::DeviceKernelFacts;
    using ghostlock::backend::cve_2026_43284::lkm::KernelRelease;
    using ghostlock::backend::cve_2026_43284::lkm::LkmImageError;
    using ghostlock::backend::cve_2026_43284::lkm::LkmPolicyError;
    using ghostlock::backend::cve_2026_43284::lkm::LkmPolicyInput;
    using ghostlock::backend::cve_2026_43284::lkm::LkmSelection;
    using ghostlock::backend::cve_2026_43284::lkm::LkmSource;
    using ghostlock::backend::cve_2026_43284::lkm::DeviceKernelFacts;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::lkm::UmhCommand;
    using ghostlock::backend::cve_2026_43284::lkm::kVermagicMaxBytes;
    using ghostlock::backend::cve_2026_43284::lkm::proc_version_has_preempt;
    using ghostlock::backend::cve_2026_43284::lkm::required_vermagic;
    using ghostlock::backend::cve_2026_43284::lkm::rewrite_vermagic;
    using ghostlock::backend::cve_2026_43284::lkm::uts_release_token;
    using ghostlock::backend::cve_2026_43284::lkm::vermagic_diff_reason_name;
    using ghostlock::backend::cve_2026_43284::lkm::VermagicDiffReason;
    using ghostlock::backend::cve_2026_43284::lkm::UmhCommandError;
    using ghostlock::backend::cve_2026_43284::lkm::build_late_load_command;
    using ghostlock::backend::cve_2026_43284::lkm::default_root_package;
    using ghostlock::backend::cve_2026_43284::lkm::find_supported_kmi;
    using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgPackageName;
    using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgRoPartitions;
    using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgSoftReboot;
    using ghostlock::backend::cve_2026_43284::lkm::kLateLoadArgsKnown;
    using ghostlock::backend::cve_2026_43284::lkm::kLkmPathTokenBundled;
    using ghostlock::backend::cve_2026_43284::lkm::kLkmPathTokenCustomFile;
    using ghostlock::backend::cve_2026_43284::lkm::kSelinuxExecContextCustom;
    using ghostlock::backend::cve_2026_43284::lkm::kSelinuxExecContextInit;
    using ghostlock::backend::cve_2026_43284::lkm::kSelinuxExecContextMax;
    using ghostlock::backend::cve_2026_43284::lkm::kSupportedKmiCount;
    using ghostlock::backend::cve_2026_43284::lkm::kSupportedKmis;
    using ghostlock::backend::cve_2026_43284::lkm::kUmhArgBytes;
    using ghostlock::backend::cve_2026_43284::lkm::parse_kernel_release;
    using ghostlock::backend::cve_2026_43284::lkm::precheck_module_bytes;
    using ghostlock::backend::cve_2026_43284::lkm::precheck_module_file;
    using ghostlock::backend::cve_2026_43284::lkm::resolve_lkm_selection;
    using ghostlock::terminal::RootProgram;
    using ghostlock::terminal::RootProgramKind;

    constexpr std::uint16_t kMachineAarch64 = 0xB7U;
    constexpr std::uint16_t kMachineX86_64 = 62U;

    /* The target release these fixtures must match in full (B5-9h-3). */
    constexpr std::string_view kRelease = "5.15.202-dirty";
    constexpr std::string_view kRequired =
            "5.15.202-dirty SMP preempt mod_unload modversions aarch64";

    const char kModinfoPositive[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=5.15.202-dirty SMP preempt mod_unload modversions aarch64\0";
    const char kModinfoMismatch[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=6.1.100-dirty SMP preempt mod_unload modversions aarch64\0";
    const char kModinfoNoName[] =
            "license=GPL\0"
            "vermagic=5.15.202-dirty SMP preempt mod_unload modversions aarch64\0";
    const char kModinfoNoVermagic[] = "license=GPL\0name=dirtyfrag\0";
    const char kModinfoOptionsTail[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=5.15.202-dirty SMP preempt mod_unload aarch64\0";

    struct ElfSpec final {
        std::string modinfo = std::string(kModinfoPositive, sizeof(kModinfoPositive) - 1U);
        bool present_modinfo = true;
        std::uint16_t machine = kMachineAarch64;
        std::uint32_t versions_size = 0U;
        /* When set, the section is named __versions and carries SHF_ALLOC, so
         * the kernel's find_sec() finds it (empty or not) and has_crcs is true. */
        bool present_versions = true;
        bool versions_alloc = false;
        std::string tail{};
    };

    std::vector<std::uint8_t> build_elf(const ElfSpec &spec) {
        std::string shstr;
        shstr.push_back('\0');
        const std::size_t shstrtab_name = shstr.size();
        shstr += ".shstrtab";
        shstr.push_back('\0');
        const std::size_t modinfo_name = shstr.size();
        shstr += ".modinfo";
        shstr.push_back('\0');
        const std::size_t versions_name = shstr.size();
        shstr += "__versions";
        shstr.push_back('\0');

        std::vector<std::uint8_t> out(64U, 0U);
        const auto put16 = [&out](std::size_t at, std::uint16_t value) {
            out[at] = static_cast<std::uint8_t>(value & 0xFFU);
            out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        };
        const auto put32 = [&out](std::size_t at, std::uint32_t value) {
            out[at] = static_cast<std::uint8_t>(value & 0xFFU);
            out[at + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            out[at + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            out[at + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        };
        const auto put64 = [&out](std::size_t at, std::uint64_t value) {
            for (std::size_t i = 0U; i < 8U; ++i) {
                out[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
            }
        };

        out[0] = 0x7FU;
        out[1] = 'E';
        out[2] = 'L';
        out[3] = 'F';
        out[4] = 2U; /* ELFCLASS64 */
        out[5] = 1U; /* little-endian */
        out[6] = 1U; /* EV_CURRENT */
        put16(0x10U, 1U); /* ET_REL */
        put16(0x12U, spec.machine);

        std::size_t pos = 64U;
        const std::size_t shstr_off = pos;
        out.insert(out.end(), shstr.begin(), shstr.end());
        pos += shstr.size();
        const std::size_t modinfo_off = pos;
        if (spec.present_modinfo) {
            out.insert(out.end(), spec.modinfo.begin(), spec.modinfo.end());
            pos += spec.modinfo.size();
        }
        const std::size_t versions_off = pos;
        out.insert(out.end(), static_cast<std::size_t>(spec.versions_size),
                   static_cast<std::uint8_t>(0));
        pos += spec.versions_size;
        while (pos % 8U != 0U) {
            out.push_back(0U);
            ++pos;
        }
        const std::size_t shoff = pos;
        out.resize(out.size() + 4U * 64U, 0U);
        put64(0x28U, shoff);
        put16(0x3AU, 64U);
        put16(0x3CU, 4U);
        put16(0x3EU, 1U);

        std::size_t section = shoff + 64U;
        put32(section, static_cast<std::uint32_t>(shstrtab_name));
        put32(section + 4U, 3U); /* SHT_STRTAB */
        put64(section + 0x18U, shstr_off);
        put64(section + 0x20U, shstr.size());

        section = shoff + 128U;
        put32(section, static_cast<std::uint32_t>(spec.present_modinfo ? modinfo_name
                                                                       : shstrtab_name));
        put32(section + 4U, 1U); /* SHT_PROGBITS */
        put64(section + 0x18U, modinfo_off);
        put64(section + 0x20U, spec.present_modinfo ? spec.modinfo.size() : 0U);

        section = shoff + 192U;
        put32(section, static_cast<std::uint32_t>(spec.present_versions ? versions_name
                                                                         : 0U));
        put32(section + 4U, 1U);
        put64(section + 0x08U, spec.versions_alloc ? 2U : 0U); /* SHF_ALLOC */
        put64(section + 0x18U, versions_off);
        put64(section + 0x20U, spec.versions_size);

        if (!spec.tail.empty()) {
            out.insert(out.end(), spec.tail.begin(), spec.tail.end());
        }
        return out;
    }

    /* The audited target facts: plain PREEMPT, CONFIG_MODVERSIONS=y and
     * CONFIG_MODULE_FORCE_UNLOAD unset -> the full required string above. */
    DeviceKernelFacts target_facts_5_15() {
        DeviceKernelFacts facts{};
        facts.release = kRelease;
        facts.preempt = true;
        facts.modversions = true;
        facts.module_force_unload = false;
        return facts;
    }

    void expect_resolve(const LkmPolicyInput &input, bool want_ok, LkmPolicyError want_error) {
        LkmSelection selection{};
        LkmPolicyError error = LkmPolicyError::None;
        const bool ok = resolve_lkm_selection(input, selection, error);
        assert(ok == want_ok);
        assert(error == want_error);
        if (want_ok) {
            assert(selection.kmi != nullptr);
        }
    }

    struct UmhSpec final {
        std::string program = "/data/adb/ksud";
        std::string package = "me.weishu.kernelsu";
        std::uint32_t late_load_args = 0U;
        std::uint32_t selinux_exec_context = kSelinuxExecContextInit;
    };

    UmhCommand build_umh(const UmhSpec &spec, UmhCommandError &error) {
        RootProgram program{};
        program.kind = RootProgramKind::KernelSU;
        program.set_argv(spec.program);
        UmhCommand command{};
        const bool ok = build_late_load_command(program, spec.package, spec.late_load_args,
                                                spec.selinux_exec_context, command, error);
        assert(ok == (error == UmhCommandError::None));
        return command;
    }

    std::string temp_path(const char *tag) {
        std::string path = "/tmp/ghostlock-lkm-";
        path += tag;
        path += "-";
        path += std::to_string(static_cast<long>(getpid()));
        path += ".ko";
        return path;
    }

    bool write_bytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return false;
        }
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const ssize_t written = write(fd, bytes.data() + offset, bytes.size() - offset);
            if (written <= 0) {
                close(fd);
                return false;
            }
            offset += static_cast<std::size_t>(written);
        }
        close(fd);
        return true;
    }
} // namespace

int main() {
    /* ---- parse_kernel_release: uname and canonical forms. ---- */
    {
        KernelRelease release{};
        assert(parse_kernel_release("5.15.202-android13-8-gabcdef", release));
        assert(release.android_release == 13U);
        assert(release.kernel_major == 5U);
        assert(release.kernel_minor == 15U);
        assert(release.kmi == 5015U);

        assert(parse_kernel_release("android14-6.1", release));
        assert(release.android_release == 14U);
        assert(release.kernel_major == 6U);
        assert(release.kernel_minor == 1U);
        assert(release.kmi == 6001U);

        assert(parse_kernel_release("5.10.205-android12-9-g0", release));
        assert(release.android_release == 12U);
        assert(release.kmi == 5010U);

        assert(parse_kernel_release("6.1.100-android14-6-g0", release));
        assert(release.android_release == 14U);
        assert(release.kernel_major == 6U);
        assert(release.kernel_minor == 1U);
        assert(release.kmi == 6001U);

        /* A release without the android suffix parses but never matches. */
        assert(parse_kernel_release("5.15.202-dirty", release));
        assert(release.android_release == 0U);
        assert(release.kmi == 5015U);

        assert(parse_kernel_release("android15-6.6.50", release));
        assert(release.android_release == 15U);
        assert(release.kernel_major == 6U);
        assert(release.kernel_minor == 6U);

        assert(!parse_kernel_release("garbage", release));
        assert(!parse_kernel_release("androidxx-5.15", release));
        assert(!parse_kernel_release("5.x-android13-9", release));
        assert(!parse_kernel_release("android13", release));
        assert(!parse_kernel_release("", release));
    }

    /* ---- supported table: exact key including same-kmi releases. ---- */
    {
        assert(kSupportedKmiCount == 8U);
        assert(find_supported_kmi(13U, 5015U) != nullptr);
        assert(find_supported_kmi(13U, 5015U)->label == "android13-5.15");
        assert(find_supported_kmi(14U, 5015U) != nullptr);
        assert(find_supported_kmi(14U, 5015U)->label == "android14-5.15");
        assert(find_supported_kmi(12U, 5010U)->label == "android12-5.10");
        assert(find_supported_kmi(13U, 5010U)->label == "android13-5.10");
        assert(find_supported_kmi(14U, 6001U)->label == "android14-6.1");
        assert(find_supported_kmi(15U, 6006U)->label == "android15-6.6");
        assert(find_supported_kmi(16U, 6012U)->label == "android16-6.12");
        assert(find_supported_kmi(17U, 6018U)->label == "android17-6.18");
        assert(find_supported_kmi(12U, 5015U) == nullptr);
        assert(find_supported_kmi(13U, 6006U) == nullptr);
        assert(find_supported_kmi(99U, 5015U) == nullptr);
    }

    /* ---- resolve_lkm_selection: fail-closed ordering and positives. ---- */
    {
        LkmPolicyInput input{};
        input.facts.release = "5.15.202-android14-6-gabc";
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        input.late_load_args_token =
                static_cast<std::uint64_t>(kLateLoadArgPackageName | kLateLoadArgRoPartitions);
        LkmSelection selection{};
        LkmPolicyError error = LkmPolicyError::None;
        assert(resolve_lkm_selection(input, selection, error));
        assert(error == LkmPolicyError::None);
        assert(selection.kmi->label == "android14-5.15");
        assert(selection.source == LkmSource::BundledKmi);
        assert(selection.release.android_release == 14U);
        assert(selection.late_load_args == (kLateLoadArgPackageName | kLateLoadArgRoPartitions));

        /* android13-5.10 must not be confused with android12-5.10. */
        input = LkmPolicyInput{};
        input.facts.release = "5.10.205-android13-8-g0";
        input.profile_kmi = 5010U;
        input.lkm_path_token = kLkmPathTokenCustomFile;
        assert(resolve_lkm_selection(input, selection, error));
        assert(selection.kmi->label == "android13-5.10");
        assert(selection.source == LkmSource::CustomFile);

        /* Canonical label and absent late_load_args default to zero. */
        input = LkmPolicyInput{};
        input.facts.release = "android15-6.6";
        input.profile_kmi = 6006U;
        input.lkm_path_token = kLkmPathTokenBundled;
        assert(resolve_lkm_selection(input, selection, error));
        assert(selection.kmi->label == "android15-6.6");
        assert(selection.late_load_args == 0U);

        /* Missing release. */
        input = LkmPolicyInput{};
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::MissingRelease);

        /* Unparsable release. */
        input = LkmPolicyInput{};
        input.facts.release = "nonsense";
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::ReleaseUnparsable);

        /* Already-patched kernel is not applicable. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android14-6-g0";
        input.facts.has_f4c50a4 = true;
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::PatchedKernel);

        /* Missing profile kmi: the KMI is a device fact, so it resolves from the
         * release instead of failing the run. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android13-8-g0";
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, true, LkmPolicyError::None);

        /* profile kmi must equal the release-derived kmi. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android13-8-g0";
        input.profile_kmi = 5010U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::KmiFieldMismatch);

        /* Unsupported KMI (android11-5.4). */
        input = LkmPolicyInput{};
        input.facts.release = "5.4.200-android11-9-g0";
        input.profile_kmi = 5004U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::UnsupportedKmi);

        /* Android suffix absent: parse succeeds but no table row matches. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-dirty";
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        expect_resolve(input, false, LkmPolicyError::UnsupportedKmi);

        /* Missing lkm_path token: the production path supplies its own module
         * mirror, so an absent token defaults to the bundled selection. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android13-8-g0";
        input.profile_kmi = 5015U;
        expect_resolve(input, true, LkmPolicyError::None);

        /* Unknown lkm_path token. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android14-6-g0";
        input.profile_kmi = 5015U;
        input.lkm_path_token = 2U;
        expect_resolve(input, false, LkmPolicyError::UnknownLkmSource);

        /* Unknown late_load_args bit. */
        input = LkmPolicyInput{};
        input.facts.release = "5.15.202-android14-6-g0";
        input.profile_kmi = 5015U;
        input.lkm_path_token = kLkmPathTokenBundled;
        input.late_load_args_token = static_cast<std::uint64_t>(kLateLoadArgsKnown) << 1U;
        expect_resolve(input, false, LkmPolicyError::UnknownLateLoadArgs);
    }

    /* ---- precheck_module_bytes: positive and each negative rule. ---- */
    {
        const DeviceKernelFacts release = target_facts_5_15();
        ModuleFacts facts{};
        LkmImageError error = LkmImageError::None;

        const std::vector<std::uint8_t> good = build_elf(ElfSpec{});
        assert(precheck_module_bytes(good.data(), good.size(), release, facts, error));
        assert(error == LkmImageError::None);
        assert(facts.elf_valid && facts.has_modinfo && facts.has_name && facts.has_vermagic);
        assert(facts.vermagic_matches && !facts.has_crcs && facts.versions_empty);
        assert(!facts.signed_module && !facts.kcfi_present);
        assert(facts.vermagic_diff == VermagicDiffReason::None);
        assert(std::string_view(facts.module_vermagic) == kRequired);
        assert(std::string_view(facts.required_vermagic) == kRequired);
        assert(!facts.vermagic_rewritten);

        /* kCFI markers are reported but do not decide pass/fail. */
        ElfSpec cfi{};
        cfi.tail = "__cfi_check";
        const std::vector<std::uint8_t> cfi_bytes = build_elf(cfi);
        assert(precheck_module_bytes(cfi_bytes.data(), cfi_bytes.size(), release, facts, error));
        assert(facts.kcfi_present);

        /* Signed module: cannot be validated host-side, fail closed. */
        ElfSpec signed_spec{};
        signed_spec.tail = "~Module signature appended~";
        const std::vector<std::uint8_t> signed_bytes = build_elf(signed_spec);
        assert(!precheck_module_bytes(signed_bytes.data(), signed_bytes.size(), release, facts,
                                      error));
        assert(error == LkmImageError::SignedModule);
        assert(facts.signed_module);

        /* A loadable (SHF_ALLOC) zero-length __versions still makes the kernel
         * take the tail-only same_magic() path; this is the real DDK module. */
        ElfSpec empty_versions{};
        empty_versions.versions_alloc = true;
        const std::vector<std::uint8_t> empty_versions_bytes = build_elf(empty_versions);
        assert(precheck_module_bytes(empty_versions_bytes.data(), empty_versions_bytes.size(),
                                     release, facts, error));
        assert(error == LkmImageError::None);
        assert(facts.has_crcs && facts.vermagic_matches && facts.versions_empty);

        /* With CRCs the UTS_RELEASE token is ignored: same tail -> accepted. */
        ElfSpec crc_release{};
        crc_release.versions_size = 4U;
        crc_release.versions_alloc = true;
        crc_release.modinfo = std::string(kModinfoMismatch, sizeof(kModinfoMismatch) - 1U);
        const std::vector<std::uint8_t> crc_release_bytes = build_elf(crc_release);
        assert(precheck_module_bytes(crc_release_bytes.data(), crc_release_bytes.size(),
                                     release, facts, error));
        assert(error == LkmImageError::None);
        assert(facts.has_crcs && !facts.versions_empty && facts.vermagic_matches);
        assert(facts.vermagic_diff == VermagicDiffReason::None);

        /* The same release mismatch without __versions is rejected in full. */
        ElfSpec nocrc_release{};
        nocrc_release.present_versions = false;
        nocrc_release.modinfo = std::string(kModinfoMismatch, sizeof(kModinfoMismatch) - 1U);
        const std::vector<std::uint8_t> nocrc_release_bytes = build_elf(nocrc_release);
        assert(!precheck_module_bytes(nocrc_release_bytes.data(), nocrc_release_bytes.size(),
                                      release, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(!facts.has_crcs && !facts.vermagic_matches);
        assert(facts.vermagic_diff == VermagicDiffReason::Release);

        /* A loadable __versions whose option tail differs is still rejected. */
        ElfSpec crc_tail{};
        crc_tail.versions_size = 4U;
        crc_tail.versions_alloc = true;
        crc_tail.modinfo = std::string(kModinfoOptionsTail,
                                       sizeof(kModinfoOptionsTail) - 1U);
        const std::vector<std::uint8_t> crc_tail_bytes = build_elf(crc_tail);
        assert(!precheck_module_bytes(crc_tail_bytes.data(), crc_tail_bytes.size(),
                                      release, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(facts.has_crcs && !facts.vermagic_matches);
        assert(facts.vermagic_diff == VermagicDiffReason::Options);

        /* A __versions without SHF_ALLOC is invisible to the kernel's find_sec()
         * and must not take the lenient tail-only path. */
        ElfSpec nonalloc_versions{};
        nonalloc_versions.versions_size = 4U;
        nonalloc_versions.versions_alloc = false;
        nonalloc_versions.modinfo = std::string(kModinfoMismatch,
                                                sizeof(kModinfoMismatch) - 1U);
        const std::vector<std::uint8_t> nonalloc_versions_bytes =
                build_elf(nonalloc_versions);
        assert(!precheck_module_bytes(nonalloc_versions_bytes.data(),
                                      nonalloc_versions_bytes.size(), release, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(!facts.has_crcs && facts.vermagic_diff == VermagicDiffReason::Release);

        /* Missing .modinfo section. */
        ElfSpec no_modinfo{};
        no_modinfo.present_modinfo = false;
        const std::vector<std::uint8_t> no_modinfo_bytes = build_elf(no_modinfo);
        assert(!precheck_module_bytes(no_modinfo_bytes.data(), no_modinfo_bytes.size(), release,
                                      facts, error));
        assert(error == LkmImageError::MissingModinfo);

        /* .modinfo without a name= entry. */
        ElfSpec no_name{};
        no_name.modinfo = std::string(kModinfoNoName, sizeof(kModinfoNoName) - 1U);
        const std::vector<std::uint8_t> no_name_bytes = build_elf(no_name);
        assert(!precheck_module_bytes(no_name_bytes.data(), no_name_bytes.size(), release, facts,
                                      error));
        assert(error == LkmImageError::MissingName);

        /* .modinfo without a vermagic= entry. */
        ElfSpec no_vermagic{};
        no_vermagic.modinfo = std::string(kModinfoNoVermagic, sizeof(kModinfoNoVermagic) - 1U);
        const std::vector<std::uint8_t> no_vermagic_bytes = build_elf(no_vermagic);
        assert(!precheck_module_bytes(no_vermagic_bytes.data(), no_vermagic_bytes.size(), release,
                                      facts, error));
        assert(error == LkmImageError::VermagicMissing);

        /* Vermagic for a different kernel: full-string mismatch, not a
         * release-prefix test, with both sides and the reason recorded. */
        ElfSpec mismatch{};
        mismatch.modinfo = std::string(kModinfoMismatch, sizeof(kModinfoMismatch) - 1U);
        const std::vector<std::uint8_t> mismatch_bytes = build_elf(mismatch);
        assert(!precheck_module_bytes(mismatch_bytes.data(), mismatch_bytes.size(), release, facts,
                                      error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(!facts.vermagic_matches);
        assert(facts.vermagic_diff == VermagicDiffReason::Release);
        assert(std::string_view(facts.module_vermagic) ==
               "6.1.100-dirty SMP preempt mod_unload modversions aarch64");
        assert(std::string_view(facts.required_vermagic) == kRequired);

        /* Only the options tail differs -> Options reason, still rejected. */
        ElfSpec options_tail{};
        options_tail.modinfo = std::string(kModinfoOptionsTail,
                                           sizeof(kModinfoOptionsTail) - 1U);
        const std::vector<std::uint8_t> options_tail_bytes = build_elf(options_tail);
        assert(!precheck_module_bytes(options_tail_bytes.data(), options_tail_bytes.size(),
                                      release, facts, error));
        assert(error == LkmImageError::VermagicMismatch);
        assert(facts.vermagic_diff == VermagicDiffReason::Options);

        /* Wrong machine. */
        ElfSpec x86{};
        x86.machine = kMachineX86_64;
        const std::vector<std::uint8_t> x86_bytes = build_elf(x86);
        assert(!precheck_module_bytes(x86_bytes.data(), x86_bytes.size(), release, facts, error));
        assert(error == LkmImageError::NotAarch64);

        /* Not an ELF and too small. */
        const std::vector<std::uint8_t> not_elf(64U, 0U);
        assert(!precheck_module_bytes(not_elf.data(), not_elf.size(), release, facts, error));
        assert(error == LkmImageError::NotElf);
        const std::vector<std::uint8_t> tiny(10U, 0U);
        assert(!precheck_module_bytes(tiny.data(), tiny.size(), release, facts, error));
        assert(error == LkmImageError::TooSmall);
    }

    /* ---- precheck_module_file: existence, regular-file and size rules. ---- */
    {
        const DeviceKernelFacts release = target_facts_5_15();
        ModuleFacts facts{};
        LkmImageError error = LkmImageError::None;

        const std::string path = temp_path("good");
        assert(write_bytes(path, build_elf(ElfSpec{})));
        assert(precheck_module_file(path, release, facts, error));
        assert(error == LkmImageError::None);
        unlink(path.c_str());

        facts = ModuleFacts{};
        assert(!precheck_module_file("/tmp/ghostlock-lkm-missing-12345.ko", release, facts, error));
        assert(error == LkmImageError::ReadFailed);

        const std::string small = temp_path("small");
        assert(write_bytes(small, std::vector<std::uint8_t>(10U, 0U)));
        assert(!precheck_module_file(small, release, facts, error));
        assert(error == LkmImageError::TooSmall);
        unlink(small.c_str());

        assert(!precheck_module_file("/tmp", release, facts, error));
        assert(error == LkmImageError::NotRegular);

        assert(!precheck_module_file("", release, facts, error));
        assert(error == LkmImageError::ReadFailed);
    }

    /* ---- B5-9h-3 required vermagic construction. ---- */
    {
        char buf[kVermagicMaxBytes] = {};
        std::size_t len = 0U;

        /* uname -r form, full flag set; only the first token is the release. */
        DeviceKernelFacts full{};
        full.release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";
        full.preempt = true;
        full.modversions = true;
        full.module_force_unload = false;
        assert(required_vermagic(full, buf, sizeof(buf), len));
        assert(std::string_view(buf, len) ==
               "5.15.189-android13-8-00016-g51bba4309aac-ab14546557 SMP preempt "
               "mod_unload modversions aarch64");

        /* /proc/version form: the release token after "Linux version ". */
        DeviceKernelFacts proc{};
        proc.release =
                "Linux version 5.15.189-android13-8-0-gabc (build) #1 SMP PREEMPT";
        proc.preempt = true;
        proc.modversions = false;
        proc.module_force_unload = true;
        assert(required_vermagic(proc, buf, sizeof(buf), len));
        assert(std::string_view(buf, len) ==
               "5.15.189-android13-8-0-gabc SMP preempt aarch64");

        /* Every optional token off: forced unload removes mod_unload and
         * modversions=off removes the CRC token. */
        DeviceKernelFacts minimal{};
        minimal.release = "6.1.100";
        minimal.preempt = false;
        minimal.modversions = false;
        minimal.module_force_unload = true;
        assert(required_vermagic(minimal, buf, sizeof(buf), len));
        assert(std::string_view(buf, len) == "6.1.100 SMP aarch64");

        /* Default facts (caller declares nothing) advertise the audited target:
         * mod_unload + modversions. */
        DeviceKernelFacts defaults{};
        defaults.release = "6.1.100";
        assert(required_vermagic(defaults, buf, sizeof(buf), len));
        assert(std::string_view(buf, len) ==
               "6.1.100 SMP mod_unload modversions aarch64");

        /* Empty release and undersized capacity fail closed with out[0]==0. */
        DeviceKernelFacts empty{};
        char tiny[8] = {'x'};
        assert(!required_vermagic(empty, tiny, sizeof(tiny), len));
        assert(tiny[0] == '\0');
        assert(!required_vermagic(full, tiny, sizeof(tiny), len));
        assert(tiny[0] == '\0');
        assert(!required_vermagic(full, nullptr, 0U, len));

        assert(uts_release_token("5.15.202-dirty extra") == "5.15.202-dirty");
        assert(uts_release_token("Linux version 5.15.189-x (b) #1 SMP") ==
               "5.15.189-x");
        assert(uts_release_token("").empty());
        assert(proc_version_has_preempt("Linux version 5.15 PREEMPT #1"));
        assert(!proc_version_has_preempt("Linux version 5.15 #1"));
        assert(vermagic_diff_reason_name(VermagicDiffReason::Release) == "Release");
    }

    /* ---- B5-9h-3 in-place vermagic rewrite. ---- */
    {
        /* Normal rewrite: a shorter value that fits the old slot is written
         * NUL-padded, the other entries and the section length are unchanged. */
        std::vector<std::uint8_t> image = build_elf(ElfSpec{});
        const std::vector<std::uint8_t> before = image;
        const std::string_view prefix = "vermagic=";
        std::size_t at = 0U;
        bool found = false;
        for (std::size_t i = 0U; i + prefix.size() <= image.size(); ++i) {
            if (std::memcmp(image.data() + i, prefix.data(), prefix.size()) == 0) {
                at = i;
                found = true;
                break;
            }
        }
        assert(found);
        const std::size_t old_value_len = kRequired.size();
        const std::size_t slot = prefix.size() + old_value_len + 1U;
        const std::string_view shorter = "5.15.202-dirty SMP modversions aarch64";
        assert(shorter.size() < old_value_len);
        LkmImageError rw_error = LkmImageError::None;
        assert(rewrite_vermagic(image.data(), image.size(), shorter, rw_error));
        assert(rw_error == LkmImageError::None);
        assert(std::memcmp(image.data() + at, prefix.data(), prefix.size()) == 0);
        assert(std::memcmp(image.data() + at + prefix.size(), shorter.data(),
                           shorter.size()) == 0);
        for (std::size_t i = shorter.size(); i < old_value_len; ++i) {
            assert(image[at + prefix.size() + i] == 0U);
        }
        /* Every byte outside the slot - including the other .modinfo entries -
         * is unchanged. */
        for (std::size_t i = 0U; i < image.size(); ++i) {
            if (i >= at && i < at + slot) {
                continue;
            }
            assert(image[i] == before[i]);
        }
        /* The rewritten image now passes the matching-required precheck. */
        DeviceKernelFacts shorter_facts{};
        shorter_facts.release = "5.15.202-dirty";
        shorter_facts.preempt = false;
        shorter_facts.modversions = true;
        shorter_facts.module_force_unload = true;
        ModuleFacts rewritten_facts{};
        rw_error = LkmImageError::None;
        assert(precheck_module_bytes(image.data(), image.size(), shorter_facts,
                                     rewritten_facts, rw_error));
        assert(rewritten_facts.vermagic_matches);
    }
    {
        /* Slot too small: required longer than "vermagic=" + old + NUL fails
         * closed with the image byte-identical. */
        std::vector<std::uint8_t> image = build_elf(ElfSpec{});
        const std::vector<std::uint8_t> before = image;
        LkmImageError error = LkmImageError::None;
        const std::string_view too_long =
                "9.9.9-deliberately-longer-required-vermagic SMP preempt mod_unload "
                "modversions aarch64";
        assert(!rewrite_vermagic(image.data(), image.size(), too_long, error));
        assert(error == LkmImageError::VermagicSlotTooSmall);
        assert(image == before);

        /* Missing vermagic= entry fails closed and leaves the image alone. */
        ElfSpec no_vermagic{};
        no_vermagic.modinfo =
                std::string(kModinfoNoVermagic, sizeof(kModinfoNoVermagic) - 1U);
        std::vector<std::uint8_t> no_v = build_elf(no_vermagic);
        const std::vector<std::uint8_t> no_v_before = no_v;
        error = LkmImageError::None;
        assert(!rewrite_vermagic(no_v.data(), no_v.size(), "x", error));
        assert(error == LkmImageError::VermagicMissing);
        assert(no_v == no_v_before);

        /* An empty required value is rejected. */
        std::vector<std::uint8_t> image2 = build_elf(ElfSpec{});
        error = LkmImageError::None;
        assert(!rewrite_vermagic(image2.data(), image2.size(), "", error));
        assert(error == LkmImageError::VermagicMissing);
    }

    /* ---- UMH argv: ksud late-load variant, boundaries and injection. ---- */
    {
        assert(default_root_package(RootProgramKind::KernelSU) == "me.weishu.kernelsu");
        assert(default_root_package(RootProgramKind::FolkPatch).empty());
        assert(default_root_package(RootProgramKind::Custom).empty());

        UmhCommandError error = UmhCommandError::None;
        UmhSpec all{};
        all.late_load_args = kLateLoadArgPackageName | kLateLoadArgRoPartitions |
                             kLateLoadArgSoftReboot;
        UmhCommand command = build_umh(all, error);
        assert(error == UmhCommandError::None);
        assert(command.argc == 6U);
        assert(command.arg(0U) == "/data/adb/ksud");
        assert(command.arg(1U) == "late-load");
        assert(command.arg(2U) == "--package-name");
        assert(command.arg(3U) == "me.weishu.kernelsu");
        assert(command.arg(4U) == "--ro-partitions");
        assert(command.arg(5U) == "--soft-reboot");
        assert(command.selinux_exec_context == kSelinuxExecContextInit);
        assert(command.argv[command.argc][0] == '\0');

        UmhSpec none{};
        command = build_umh(none, error);
        assert(error == UmhCommandError::None);
        assert(command.argc == 2U);
        assert(command.arg(0U) == "/data/adb/ksud");
        assert(command.arg(1U) == "late-load");

        UmhSpec ro{};
        ro.late_load_args = kLateLoadArgRoPartitions;
        command = build_umh(ro, error);
        assert(command.argc == 3U);
        assert(command.arg(2U) == "--ro-partitions");

        UmhSpec custom_context{};
        custom_context.selinux_exec_context = kSelinuxExecContextCustom;
        command = build_umh(custom_context, error);
        assert(error == UmhCommandError::None);
        assert(command.selinux_exec_context == kSelinuxExecContextCustom);

        /* Exactly kUmhArgBytes - 1 is accepted; one more fails closed. */
        UmhSpec boundary{};
        boundary.program = std::string(kUmhArgBytes - 1U, 'a');
        command = build_umh(boundary, error);
        assert(error == UmhCommandError::None);
        boundary.program = std::string(kUmhArgBytes, 'b');
        static_cast<void>(build_umh(boundary, error));
        assert(error == UmhCommandError::ArgTooLong);

        /* Missing root program path. */
        UmhSpec empty_program{};
        empty_program.program.clear();
        static_cast<void>(build_umh(empty_program, error));
        assert(error == UmhCommandError::MissingRootProgram);

        /* --package-name with no validated package string. */
        UmhSpec no_package{};
        no_package.package.clear();
        no_package.late_load_args = kLateLoadArgPackageName;
        static_cast<void>(build_umh(no_package, error));
        assert(error == UmhCommandError::MissingPackageName);

        /* Unknown late_load_args bit. */
        UmhSpec unknown_bit{};
        unknown_bit.late_load_args = 1U << 7U;
        static_cast<void>(build_umh(unknown_bit, error));
        assert(error == UmhCommandError::UnknownLateLoadArgs);

        /* Unknown SELinux exec context. */
        UmhSpec bad_context{};
        bad_context.selinux_exec_context = kSelinuxExecContextMax + 1U;
        static_cast<void>(build_umh(bad_context, error));
        assert(error == UmhCommandError::UnknownSelinuxContext);

        /* Control bytes are rejected: no shell, but no smuggling either. */
        UmhSpec newline{};
        newline.program = "/data/adb/ksud\n";
        static_cast<void>(build_umh(newline, error));
        assert(error == UmhCommandError::ArgInvalid);

        UmhSpec semicolon{};
        semicolon.package = "me.weishu.kernelsu; id";
        semicolon.late_load_args = kLateLoadArgPackageName;
        /* ';' and ' ' are printable, so the argv stays a single inert element. */
        command = build_umh(semicolon, error);
        assert(error == UmhCommandError::None);
        assert(command.arg(3U) == "me.weishu.kernelsu; id");
    }

    /* ---- Real DDK module self-proof: kernel same_magic() tail rule. ---- */
    {
        constexpr const char kRealModinfo[] =
                "license=GPL\0name=ghostlock\0"
                "vermagic=5.15.202-android13-5.15.202_r00-dirty SMP preempt "
                "mod_unload modversions aarch64\0";
        DeviceKernelFacts device{};
        device.release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";
        device.preempt = true;
        device.modversions = true;
        device.module_force_unload = false;

        /* Deterministic equivalent of the real module: a CRC-bearing .ko whose
         * UTS_RELEASE differs but whose pre-first-space tail matches. */
        ElfSpec synthetic{};
        synthetic.modinfo = std::string(kRealModinfo, sizeof(kRealModinfo) - 1U);
        synthetic.versions_size = 8U;
        synthetic.versions_alloc = true;
        const std::vector<std::uint8_t> synth_bytes = build_elf(synthetic);
        ModuleFacts synth_facts{};
        LkmImageError synth_error = LkmImageError::None;
        const bool synth_ok = precheck_module_bytes(synth_bytes.data(), synth_bytes.size(),
                                                    device, synth_facts, synth_error);
        std::printf("[self-proof] synthetic CRC .ko: ok=%d has_crcs=%d "
                    "versions_empty=%d match=%d error=%d\n",
                    synth_ok ? 1 : 0, synth_facts.has_crcs ? 1 : 0,
                    synth_facts.versions_empty ? 1 : 0,
                    synth_facts.vermagic_matches ? 1 : 0,
                    static_cast<int>(synth_error));
        assert(synth_ok && synth_facts.has_crcs && synth_facts.vermagic_matches);

        /* The real artifact when present (the host test runs under src/ or the
         * repo root depending on the caller). */
        const char *candidates[] = {
            "tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko",
            "../tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko",
            "../../tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko",
            "../../../tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko",
        };
        const char *real_ko = nullptr;
        for (const char *candidate : candidates) {
            if (::access(candidate, R_OK) == 0) {
                real_ko = candidate;
                break;
            }
        }
        if (real_ko != nullptr) {
            ModuleFacts real_facts{};
            LkmImageError real_error = LkmImageError::None;
            const bool real_ok = precheck_module_file(real_ko, device, real_facts, real_error);
            std::printf("[self-proof] real .ko %s: ok=%d has_crcs=%d "
                        "versions_empty=%d match=%d error=%d\n",
                        real_ko, real_ok ? 1 : 0, real_facts.has_crcs ? 1 : 0,
                        real_facts.versions_empty ? 1 : 0,
                        real_facts.vermagic_matches ? 1 : 0,
                        static_cast<int>(real_error));
            assert(real_ok);
            assert(real_facts.has_crcs);
            assert(real_facts.vermagic_matches);
            assert(real_facts.versions_empty);
        } else {
            std::printf("[self-proof] real .ko not present; synthetic only\n");
        }
    }

    std::puts("cve_2026_43284_lkm_test: OK");
    return 0;
}
