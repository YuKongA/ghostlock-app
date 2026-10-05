/* Host tests for B5-9a: the read-only CVE-2026-43284 diagnostic entry point,
 * the CLI parser it hangs off, and the read-only ChainOps binding.
 *
 * No device, no module load, no fork/exec and no production write: the device
 * probe is a fake and the .ko fixture is synthesized ELF bytes in /tmp. Covers
 * CLI mode/conflict parsing, the diagnostic report structure and exit code,
 * .ko precheck positives/negatives, the pread read-only binding, and the fact
 * that the backend stays unavailable (selection_supported == false). */

#include "backend/cve_2026_43284/diagnostic.hpp"
#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "pipeline/component_catalog.hpp"
#include "platform/device_facts.hpp"
#include "support/cli.hpp"

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
    using ghostlock::backend::cve_2026_43284::ReadOnlyChainContext;
    using ghostlock::backend::cve_2026_43284::diagnostic::DiagnosticOutcome;
    using ghostlock::backend::cve_2026_43284::diagnostic::DiagnosticReport;
    using ghostlock::backend::cve_2026_43284::diagnostic::device_fact_error_name;
    using ghostlock::backend::cve_2026_43284::diagnostic::diagnostic_exit_code;
    using ghostlock::backend::cve_2026_43284::diagnostic::format_diagnostic;
    using ghostlock::backend::cve_2026_43284::diagnostic::kDiagnosticMarker;
    using ghostlock::backend::cve_2026_43284::diagnostic::lkm_image_error_name;
    using ghostlock::backend::cve_2026_43284::diagnostic::lkm_policy_error_name;
    using ghostlock::backend::cve_2026_43284::diagnostic::run_device_diagnostic;
    using ghostlock::backend::cve_2026_43284::make_read_only_chain_ops;
    using ghostlock::backend::cve_2026_43284::real_read_block;
    using ghostlock::backend::cve_2026_43284::lkm::KernelRelease;
    using ghostlock::backend::cve_2026_43284::lkm::LkmImageError;
    using ghostlock::backend::cve_2026_43284::lkm::LkmPolicyError;
    using ghostlock::backend::cve_2026_43284::lkm::ModuleFacts;
    using ghostlock::backend::cve_2026_43284::steps::ChainOps;
    using ghostlock::platform::DeviceFactError;
    using ghostlock::platform::DeviceProbeOps;
    using ghostlock::platform::FileFact;
    using ghostlock::platform::VendorCandidate;
    using ghostlock::support::cli::Mode;
    using ghostlock::support::cli::Options;
    using ghostlock::support::cli::ParseError;

    constexpr std::uint16_t kMachineAarch64 = 0xB7U;

    /* Must equal the full required vermagic built from FakeDevice's release
     * ("5.15.202-android14-6-gabc") plus preempt (proc_version PREEMPT) and the
     * audited defaults (mod_unload, modversions). */
    const char kModinfoGood[] =
            "license=GPL\0name=dirtyfrag\0"
            "vermagic=5.15.202-android14-6-gabc SMP preempt mod_unload modversions "
            "aarch64\0";

    struct ElfSpec final {
        std::string modinfo = std::string(kModinfoGood, sizeof(kModinfoGood) - 1U);
        bool present_modinfo = true;
        std::uint16_t machine = kMachineAarch64;
        std::uint32_t versions_size = 0U;
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
        out[4] = 2U;
        out[5] = 1U;
        out[6] = 1U;
        put16(0x10U, 1U);
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
        put32(section + 4U, 3U);
        put64(section + 0x18U, shstr_off);
        put64(section + 0x20U, shstr.size());

        section = shoff + 128U;
        put32(section, static_cast<std::uint32_t>(spec.present_modinfo ? modinfo_name
                                                                       : shstrtab_name));
        put32(section + 4U, 1U);
        put64(section + 0x18U, modinfo_off);
        put64(section + 0x20U, spec.present_modinfo ? spec.modinfo.size() : 0U);

        section = shoff + 192U;
        put32(section, static_cast<std::uint32_t>(versions_name));
        put32(section + 4U, 1U);
        put64(section + 0x18U, versions_off);
        put64(section + 0x20U, spec.versions_size);

        if (!spec.tail.empty()) {
            out.insert(out.end(), spec.tail.begin(), spec.tail.end());
        }
        return out;
    }

    struct FakeDevice final {
        std::string release = "5.15.202-android14-6-gabc";
        std::string proc_version =
                "Linux version 5.15.202-android14-6-gabc (build@host) #1 SMP PREEMPT";
        bool selinux_readable = true;
        int selinux_enforce = 0;
        bool crash_exists = true;
        bool crash_label_known = true;
        std::string crash_label = "u:object_r:crash_dump_exec:s0";
        bool crash_verity = false;
        std::vector<std::string> vendor_paths{"/vendor/lib64/libbinderdebug.so"};
        std::vector<std::string> vendor_labels{"u:object_r:vendor_file:s0"};
        std::vector<std::string> symbols{"selinux_state", "task_defex_enforce",
                                         "task_defex_user_exec", "get_dc_target_dpath"};
    };

    long fake_read_release(void *raw, char *out, std::size_t capacity) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        if (device->release.size() + 1U > capacity) {
            return -1;
        }
        std::memcpy(out, device->release.data(), device->release.size());
        out[device->release.size()] = '\0';
        return static_cast<long>(device->release.size());
    }

    long fake_read_proc_version(void *raw, char *out, std::size_t capacity) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        if (device->proc_version.size() + 1U > capacity) {
            return -1;
        }
        std::memcpy(out, device->proc_version.data(), device->proc_version.size());
        out[device->proc_version.size()] = '\0';
        return static_cast<long>(device->proc_version.size());
    }

    int fake_read_selinux(void *raw) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        return device->selinux_readable ? device->selinux_enforce : -1;
    }

    bool fake_file_fact(void *raw, const char *path, FileFact &out) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        out = FileFact{};
        if (path == nullptr) {
            return false;
        }
        if (std::string_view(path) != std::string_view(ghostlock::platform::kCrashDump64Path)) {
            return true;
        }
        out.path.set(path);
        out.exists = device->crash_exists;
        if (device->crash_exists && device->crash_label_known) {
            out.label.set(device->crash_label);
            out.label_known = true;
        }
        out.verity = device->crash_verity;
        return true;
    }

    std::size_t fake_list_vendor(void *raw, VendorCandidate *out,
                                 std::size_t capacity) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        if (out == nullptr || capacity == 0U) {
            return 0U;
        }
        const std::size_t limit = device->vendor_paths.size() < capacity
                                          ? device->vendor_paths.size()
                                          : capacity;
        for (std::size_t i = 0U; i < limit; ++i) {
            out[i] = VendorCandidate{};
            out[i].path.set(device->vendor_paths[i]);
            const std::string_view label = i < device->vendor_labels.size()
                                                   ? std::string_view(device->vendor_labels[i])
                                                   : std::string_view{};
            out[i].label.set(label);
            out[i].exists = true;
            out[i].vendor_file_label = label.find("vendor_file") != std::string_view::npos;
        }
        return limit;
    }

    bool fake_symbol_present(void *raw, const char *symbol) noexcept {
        auto *device = static_cast<FakeDevice *>(raw);
        if (symbol == nullptr) {
            return false;
        }
        const std::string_view name(symbol);
        for (const std::string &present : device->symbols) {
            if (present == name) {
                return true;
            }
        }
        return false;
    }

    DeviceProbeOps fake_ops(FakeDevice &device) noexcept {
        DeviceProbeOps ops{};
        ops.ctx = &device;
        ops.read_release = fake_read_release;
        ops.read_proc_version = fake_read_proc_version;
        ops.read_selinux_enforce = fake_read_selinux;
        ops.file_fact = fake_file_fact;
        ops.list_vendor_candidates = fake_list_vendor;
        ops.symbol_present = fake_symbol_present;
        return ops;
    }

    std::string temp_ko_path(const char *tag) {
        std::string path = "/tmp/ghostlock-diag-";
        path += tag;
        path += "-";
        path += std::to_string(static_cast<long>(getpid()));
        path += ".ko";
        return path;
    }

    bool write_bytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return false;
        }
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const ssize_t written =
                    ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (written <= 0) {
                ::close(fd);
                return false;
            }
            offset += static_cast<std::size_t>(written);
        }
        ::close(fd);
        return true;
    }

    ParseError parse_args(std::vector<std::string> args, Options &opts) {
        /* The parsed Options stores raw char* into these strings, so the
         * backing storage must outlive the call; a function-local static keeps
         * the pointers valid until the next parse (the test asserts first). */
        static std::vector<std::string> storage;
        storage = std::move(args);
        static char program[] = "ghostlock";
        std::vector<char *> argv;
        argv.reserve(storage.size() + 1U);
        argv.push_back(program);
        for (std::string &arg : storage) {
            argv.push_back(arg.data());
        }
        ParseError error = ParseError::None;
        (void)ghostlock::support::cli::parse_arguments(static_cast<int>(argv.size()),
                                                      argv.data(), opts, error);
        return error;
    }
} // namespace

int main() {
    /* ---- CLI parsing: modes, conflicts and the preserved status rule. ---- */
    {
        Options opts{};
        assert(parse_args({}, opts) == ParseError::None);
        assert(opts.mode == Mode::None);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call"}, opts) == ParseError::None);
        assert(opts.mode == Mode::AppCall);
    }
    {
        Options opts{};
        assert(parse_args({"--load-prebuilt-profile", "/tmp/p.bin", "--force-attack"}, opts) ==
               ParseError::None);
        assert(opts.mode == Mode::PrebuiltFile);
        assert(std::string_view(opts.load_prebuilt_profile) == "/tmp/p.bin");
        assert(opts.force_attack);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko"}, opts) == ParseError::None);
        assert(opts.mode == Mode::ProbeCve2026_43284);
        assert(std::string_view(opts.probe_module_path) == "/tmp/a.ko");
        assert(!opts.force_attack);
        assert(!opts.status_record);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko", "--ghostlock-app-call"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--load-prebuilt-profile", "x"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko", "--force-attack"}, opts) ==
               ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko", "--enable-status-record"},
                          opts) == ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko", "--dump-kernel-log", "d"},
                          opts) == ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--enable-status-record"}, opts) == ParseError::StatusRequiresAppCall);
    }
    {
        Options opts{};
        assert(parse_args({"--nope"}, opts) == ParseError::UnknownArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--load-prebuilt-profile"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--enable-status-record",
                           "--dump-kernel-log", "d"}, opts) == ParseError::None);
        assert(opts.mode == Mode::AppCall);
        assert(opts.status_record);
        assert(std::string_view(opts.dump_kernel_log) == "d");
    }

    /* ---- Catalog: 43284 stays unavailable / not executable. ---- */
    {
        using ghostlock::contract::BackendKind;
        using ghostlock::contract::ComponentSelection;
        using ghostlock::contract::StepSetKind;
        using ghostlock::contract::TerminalKind;
        const ComponentSelection selection{BackendKind::Cve2026_43284,
                                           StepSetKind::PageCacheWrite,
                                           TerminalKind::UmhForward};
        assert(!ghostlock::contract::selection_supported(selection));
        assert(!ghostlock::contract::backend_available(BackendKind::Cve2026_43284));
        /* The triple stays catalogued/wired, so wiring and availability remain
         * two distinct facts. */
        assert(ghostlock::pipeline::combination_supported(selection));
    }

    /* ---- Read-only ChainOps: pread works; every write/trigger/wait is null. ---- */
    {
        const std::string path = temp_ko_path("read");
        std::vector<std::uint8_t> bytes(48U);
        for (std::size_t i = 0U; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::uint8_t>(i);
        }
        assert(write_bytes(path, bytes));
        const int fd = ::open(path.c_str(), O_RDONLY);
        assert(fd >= 0);
        ReadOnlyChainContext ctx{};
        ctx.fd = fd;
        const ChainOps ops = make_read_only_chain_ops(ctx);
        assert(ops.read_ready());
        assert(!ops.write_ready());
        assert(!ops.run_ready());
        assert(ops.write.write16 == nullptr);
        assert(ops.trigger == nullptr);
        assert(ops.wait_result == nullptr);
        assert(ops.release == nullptr);
        std::uint8_t block[16] = {};
        assert(ops.read_block(ops.write.ctx, 16U, block) == 16);
        for (std::size_t i = 0U; i < 16U; ++i) {
            assert(block[i] == static_cast<std::uint8_t>(16U + i));
        }
        std::uint8_t tail[16] = {};
        assert(ops.read_block(ops.write.ctx, 40U, tail) < 0);
        ReadOnlyChainContext bad{};
        const ChainOps bad_ops = make_read_only_chain_ops(bad);
        assert(real_read_block(bad_ops.write.ctx, 0U, tail) < 0);
        assert(real_read_block(nullptr, 0U, tail) < 0);
        assert(real_read_block(ops.write.ctx, 0U, nullptr) < 0);
        ::close(fd);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: a fully matching device + .ko is Ready. ---- */
    {
        FakeDevice device;
        const std::string path = temp_ko_path("good");
        assert(write_bytes(path, build_elf(ElfSpec{})));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::Ready);
        assert(report.facts_present);
        assert(report.fact_error == DeviceFactError::None);
        assert(report.release_parsed);
        assert(report.release.android_release == 14U);
        assert(report.release.kmi == 5015U);
        assert(report.kmi_resolved);
        assert(report.kmi != nullptr);
        assert(report.kmi->label == "android14-5.15");
        assert(report.lkm_error == LkmPolicyError::None);
        assert(report.module_checked);
        assert(report.image_error == LkmImageError::None);
        assert(report.module_facts.elf_valid);
        assert(report.module_facts.has_modinfo);
        assert(report.module_facts.has_name);
        assert(report.module_facts.vermagic_matches);
        assert(report.module_facts.versions_empty);
        assert(!report.module_facts.signed_module);
        assert(diagnostic_exit_code(report) == 0);

        const std::string text = format_diagnostic(report);
        assert(!text.empty());
        assert(text.front() == kDiagnosticMarker);
        assert(text.find("cve_2026_43284_diag probe") != std::string::npos);
        assert(text.find("diag.device_facts present") != std::string::npos);
        assert(text.find("diag.device_release 5.15.202-android14-6-gabc") !=
               std::string::npos);
        assert(text.find("diag.device_crash_dump64 exists=1 label=u:object_r:crash_dump_exec:s0 "
                         "verity=0") != std::string::npos);
        assert(text.find("diag.device_vendor_candidate index=0 "
                         "path=/vendor/lib64/libbinderdebug.so "
                         "label=u:object_r:vendor_file:s0 vendor_file=1") !=
               std::string::npos);
        assert(text.find("diag.device_symbols kallsyms_restricted=0 selinux_state=1") !=
               std::string::npos);
        assert(text.find("diag.lkm_release parsed=1 android=14 kmi=5015") !=
               std::string::npos);
        assert(text.find("diag.lkm_selection matched error=None label=android14-5.15") !=
               std::string::npos);
        assert(text.find("diag.module_precheck pass error=None") != std::string::npos);
        assert(text.find("cve_2026_43284_diag ready") != std::string::npos);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: unsupported KMI is rejected, module still inspected. ---- */
    {
        FakeDevice device;
        device.release = "5.4.200-android11-9-g0";
        const std::string path = temp_ko_path("kmi");
        assert(write_bytes(path, build_elf(ElfSpec{})));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::ModuleRejected);
        assert(report.facts_present);
        assert(report.release_parsed);
        assert(report.release.kmi == 5004U);
        assert(!report.kmi_resolved);
        assert(report.lkm_error == LkmPolicyError::UnsupportedKmi);
        assert(diagnostic_exit_code(report) == 3);
        const std::string text = format_diagnostic(report);
        assert(text.find("diag.lkm_selection rejected error=UnsupportedKmi") !=
               std::string::npos);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: already-patched kernel (f4c50a4 marker). ---- */
    {
        FakeDevice device;
        device.proc_version = "Linux version 5.15.202 f4c50a4 (build@host)";
        const std::string path = temp_ko_path("patched");
        assert(write_bytes(path, build_elf(ElfSpec{})));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::ModuleRejected);
        assert(report.facts_present);
        assert(!report.kmi_resolved);
        assert(report.lkm_error == LkmPolicyError::PatchedKernel);
        const std::string text = format_diagnostic(report);
        assert(text.find("has_f4c50a4=1") != std::string::npos);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: signed .ko fails closed with facts recorded. ---- */
    {
        FakeDevice device;
        const std::string path = temp_ko_path("signed");
        ElfSpec spec{};
        spec.tail = "~Module signature appended~";
        assert(write_bytes(path, build_elf(spec)));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::ModuleRejected);
        assert(report.kmi_resolved);
        assert(!report.module_checked);
        assert(report.image_error == LkmImageError::SignedModule);
        assert(report.module_facts.signed_module);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: a non-ELF .ko fails closed. ---- */
    {
        FakeDevice device;
        const std::string path = temp_ko_path("notelf");
        assert(write_bytes(path, std::vector<std::uint8_t>(64U, 0U)));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::ModuleRejected);
        assert(report.image_error == LkmImageError::NotElf);
        ::unlink(path.c_str());
    }

    /* ---- Diagnostic: each mandatory device fact is fail-closed. ---- */
    {
        FakeDevice device;
        device.crash_exists = false;
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), "/tmp/x.ko");
        assert(report.outcome == DiagnosticOutcome::DeviceBlocked);
        assert(!report.facts_present);
        assert(report.fact_error == DeviceFactError::CrashDumpMissing);
        assert(diagnostic_exit_code(report) == 2);
    }
    {
        FakeDevice device;
        device.vendor_paths.clear();
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), "/tmp/x.ko");
        assert(report.fact_error == DeviceFactError::VendorCandidatesMissing);
    }
    {
        /* Symbol absence (restricted kallsyms, or SELinux disabled via LKM) is
         * recorded but never blocks: with every other fact complete the
         * diagnostic must still reach Ready. */
        FakeDevice device;
        device.symbols.clear();
        const std::string path = temp_ko_path("restricted");
        assert(write_bytes(path, build_elf(ElfSpec{})));
        const DiagnosticReport report = run_device_diagnostic(fake_ops(device), path);
        assert(report.outcome == DiagnosticOutcome::Ready);
        assert(report.facts_present);
        assert(report.fact_error == DeviceFactError::None);
        assert(diagnostic_exit_code(report) == 0);
        const std::string text = format_diagnostic(report);
        assert(text.find("diag.device_symbols kallsyms_restricted=1 selinux_state=0") !=
               std::string::npos);
        ::unlink(path.c_str());
    }
    {
        const DeviceProbeOps empty{};
        const DiagnosticReport report = run_device_diagnostic(empty, "/tmp/x.ko");
        assert(report.outcome == DiagnosticOutcome::DeviceBlocked);
        assert(report.fact_error == DeviceFactError::Unavailable);
        assert(diagnostic_exit_code(report) == 2);
    }
    {
        /* On a non-Linux host real_device_probe() is the all-null surface, so
         * the real diagnostic must report DeviceBlocked, never Ready. */
        const DeviceProbeOps real = ghostlock::platform::real_device_probe();
        if (!real.available()) {
            const DiagnosticReport report = run_device_diagnostic(real, "/tmp/none.ko");
            assert(report.outcome == DiagnosticOutcome::DeviceBlocked);
            assert(report.fact_error == DeviceFactError::Unavailable);
        }
    }

    /* ---- Enum spellings are stable. ---- */
    {
        assert(device_fact_error_name(DeviceFactError::CrashDumpMissing) ==
               "CrashDumpMissing");
        assert(lkm_policy_error_name(LkmPolicyError::UnsupportedKmi) == "UnsupportedKmi");
        assert(lkm_image_error_name(LkmImageError::SignedModule) == "SignedModule");
        assert(lkm_image_error_name(LkmImageError::None) == "None");
    }

    std::puts("cve_2026_43284_diagnostic_test: OK");
    return 0;
}
