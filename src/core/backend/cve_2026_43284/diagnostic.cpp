/* B5-9a read-only device diagnostic for CVE-2026-43284 -- implementation.
 *
 * Pure composition over the injected DeviceProbeOps plus two file reads (the
 * .ko precheck). No write, no fork, no exec, no module load. See
 * diagnostic.hpp for the contract. */

#include "backend/cve_2026_43284/diagnostic.hpp"

#include "support/run_state.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::diagnostic {
    namespace {
        char bool_digit(bool value) noexcept { return value ? '1' : '0'; }

        void append_event(std::string &out, std::string_view step, std::string_view status) {
            out += ghostlock::support::run_state::format_event(step, status);
        }

        void append_bool(std::string &out, bool value) { out.push_back(bool_digit(value)); }

        std::string_view text_or_dash(std::string_view text) noexcept {
            return text.empty() ? std::string_view("-") : text;
        }
    } // namespace

    std::string_view device_fact_error_name(platform::DeviceFactError error) noexcept {
        switch (error) {
            case platform::DeviceFactError::None: return "None";
            case platform::DeviceFactError::Unavailable: return "Unavailable";
            case platform::DeviceFactError::ReleaseMissing: return "ReleaseMissing";
            case platform::DeviceFactError::ProcVersionMissing: return "ProcVersionMissing";
            case platform::DeviceFactError::SelinuxMissing: return "SelinuxMissing";
            case platform::DeviceFactError::CrashDumpMissing: return "CrashDumpMissing";
            case platform::DeviceFactError::CrashDumpLabelUnknown:
                return "CrashDumpLabelUnknown";
            case platform::DeviceFactError::VendorCandidatesMissing:
                return "VendorCandidatesMissing";
            case platform::DeviceFactError::SelinuxStateMissing: return "SelinuxStateMissing";
        }
        return "Unknown";
    }

    std::string_view lkm_policy_error_name(lkm::LkmPolicyError error) noexcept {
        switch (error) {
            case lkm::LkmPolicyError::None: return "None";
            case lkm::LkmPolicyError::MissingRelease: return "MissingRelease";
            case lkm::LkmPolicyError::ReleaseUnparsable: return "ReleaseUnparsable";
            case lkm::LkmPolicyError::PatchedKernel: return "PatchedKernel";
            case lkm::LkmPolicyError::MissingProfileKmi: return "MissingProfileKmi";
            case lkm::LkmPolicyError::KmiFieldMismatch: return "KmiFieldMismatch";
            case lkm::LkmPolicyError::UnsupportedKmi: return "UnsupportedKmi";
            case lkm::LkmPolicyError::MissingLkmPath: return "MissingLkmPath";
            case lkm::LkmPolicyError::UnknownLkmSource: return "UnknownLkmSource";
            case lkm::LkmPolicyError::UnknownLateLoadArgs: return "UnknownLateLoadArgs";
        }
        return "Unknown";
    }

    std::string_view lkm_image_error_name(lkm::LkmImageError error) noexcept {
        switch (error) {
            case lkm::LkmImageError::None: return "None";
            case lkm::LkmImageError::ReadFailed: return "ReadFailed";
            case lkm::LkmImageError::NotRegular: return "NotRegular";
            case lkm::LkmImageError::TooSmall: return "TooSmall";
            case lkm::LkmImageError::TooLarge: return "TooLarge";
            case lkm::LkmImageError::NotElf: return "NotElf";
            case lkm::LkmImageError::NotAarch64: return "NotAarch64";
            case lkm::LkmImageError::MissingModinfo: return "MissingModinfo";
            case lkm::LkmImageError::MissingName: return "MissingName";
            case lkm::LkmImageError::VermagicMissing: return "VermagicMissing";
            case lkm::LkmImageError::VermagicMismatch: return "VermagicMismatch";
            case lkm::LkmImageError::VermagicSlotTooSmall:
                return "VermagicSlotTooSmall";
            case lkm::LkmImageError::NonEmptyVersions: return "NonEmptyVersions";
            case lkm::LkmImageError::SignedModule: return "SignedModule";
        }
        return "Unknown";
    }

    DiagnosticReport run_device_diagnostic(const platform::DeviceProbeOps &device,
                                           std::string_view module_path) noexcept {
        DiagnosticReport report{};
        report.module_path = module_path;
        report.fact_error = platform::collect_device_facts(device, report.facts);
        if (report.fact_error != platform::DeviceFactError::None) {
            report.outcome = DiagnosticOutcome::DeviceBlocked;
            return report;
        }
        report.facts_present = true;
        report.release_parsed =
                lkm::parse_kernel_release(report.facts.release.view(), report.release);

        lkm::LkmPolicyInput input{};
        input.facts.release = report.facts.release.view();
        input.facts.has_f4c50a4 = report.facts.has_f4c50a4;
        if (report.release_parsed) {
            input.profile_kmi = report.release.kmi;
        }
        /* The operator names an explicit .ko, so the custom-file delivery token
         * is the only one that can apply. */
        input.lkm_path_token = lkm::kLkmPathTokenCustomFile;
        lkm::LkmSelection selection{};
        report.kmi_resolved = lkm::resolve_lkm_selection(input, selection, report.lkm_error);
        if (report.kmi_resolved) {
            report.kmi = selection.kmi;
        }

        if (module_path.empty()) {
            report.image_error = lkm::LkmImageError::ReadFailed;
        } else if (report.release_parsed) {
            /* B5-9h-3: the precheck uses the kernel's same_magic() rule
             * (tail-only when the module carries a loadable __versions, full
             * string otherwise). preempt comes from /proc/version;
             * modversions/module_force_unload are the audited-target defaults in
             * DeviceKernelFacts. */
            lkm::DeviceKernelFacts required{};
            required.release = report.facts.release.view();
            required.preempt = lkm::proc_version_has_preempt(
                    report.facts.proc_version.view());
            report.module_checked = lkm::precheck_module_file(
                    module_path, required, report.module_facts, report.image_error);
        }

        if (!report.release_parsed || !report.kmi_resolved || !report.module_checked) {
            report.outcome = DiagnosticOutcome::ModuleRejected;
            return report;
        }
        report.outcome = DiagnosticOutcome::Ready;
        return report;
    }

    std::string format_diagnostic(const DiagnosticReport &report) {
        std::string out;
        out.reserve(1024U);
        append_event(out, "cve_2026_43284_diag", "probe");

        {
            std::string status;
            if (report.fact_error == platform::DeviceFactError::None) {
                status = "present";
            } else if (report.facts_present) {
                status = "incomplete error=";
                status += device_fact_error_name(report.fact_error);
            } else {
                status = "unavailable error=";
                status += device_fact_error_name(report.fact_error);
            }
            append_event(out, "diag.device_facts", status);
        }

        const platform::DeviceFacts &facts = report.facts;
        if (facts.release_present) {
            append_event(out, "diag.device_release", facts.release.view());
        }
        if (facts.proc_version_present) {
            std::string status(facts.proc_version.view());
            status += " has_f4c50a4=";
            append_bool(status, facts.has_f4c50a4);
            append_event(out, "diag.device_proc_version", status);
        }
        if (facts.selinux_enforce_readable) {
            std::string status = "enforce=";
            status += std::to_string(facts.selinux_enforce);
            append_event(out, "diag.device_selinux", status);
        }
        {
            std::string status = "exists=";
            append_bool(status, facts.crash_dump.exists);
            status += " label=";
            status += text_or_dash(facts.crash_dump.label.view());
            status += " verity=";
            append_bool(status, facts.crash_dump.verity);
            append_event(out, "diag.device_crash_dump64", status);
        }
        for (std::size_t i = 0U; i < facts.vendor_candidate_count; ++i) {
            const platform::VendorCandidate &candidate = facts.vendor_candidates[i];
            std::string status = "index=";
            status += std::to_string(i);
            status += " path=";
            status += text_or_dash(candidate.path.view());
            status += " label=";
            status += text_or_dash(candidate.label.view());
            status += " vendor_file=";
            append_bool(status, candidate.vendor_file_label);
            append_event(out, "diag.device_vendor_candidate", status);
        }
        {
            std::string status = "kallsyms_restricted=";
            append_bool(status, facts.symbols.kallsyms_restricted);
            status += " selinux_state=";
            append_bool(status, facts.symbols.selinux_state);
            status += " task_defex_enforce=";
            append_bool(status, facts.symbols.defex_task_defex_enforce);
            status += " task_defex_user_exec=";
            append_bool(status, facts.symbols.defex_task_defex_user_exec);
            status += " get_dc_target_dpath=";
            append_bool(status, facts.symbols.defex_get_dc_target_dpath);
            append_event(out, "diag.device_symbols", status);
        }
        {
            std::string status = "parsed=";
            append_bool(status, report.release_parsed);
            status += " android=";
            status += std::to_string(report.release.android_release);
            status += " kmi=";
            status += std::to_string(report.release.kmi);
            append_event(out, "diag.lkm_release", status);
        }
        {
            std::string status;
            if (report.kmi_resolved) {
                status = "matched error=";
                status += lkm_policy_error_name(report.lkm_error);
                status += " label=";
                status += report.kmi != nullptr ? report.kmi->label : std::string_view("-");
            } else {
                status = "rejected error=";
                status += lkm_policy_error_name(report.lkm_error);
            }
            append_event(out, "diag.lkm_selection", status);
        }
        {
            std::string status = "path=";
            status += text_or_dash(report.module_path);
            status += " checked=";
            append_bool(status, report.module_checked);
            append_event(out, "diag.module", status);
        }
        {
            std::string status = report.module_checked ? "pass error=" : "fail error=";
            status += lkm_image_error_name(report.image_error);
            const lkm::ModuleFacts &module = report.module_facts;
            status += " elf=";
            append_bool(status, module.elf_valid);
            status += " modinfo=";
            append_bool(status, module.has_modinfo);
            status += " name=";
            append_bool(status, module.has_name);
            status += " vermagic=";
            append_bool(status, module.has_vermagic);
            status += " match=";
            append_bool(status, module.vermagic_matches);
            status += " ver_diff=";
            status += lkm::vermagic_diff_reason_name(module.vermagic_diff);
            status += " ko_vermagic=";
            status += module.module_vermagic[0] != '\0'
                              ? std::string_view(module.module_vermagic)
                              : std::string_view("-");
            status += " req_vermagic=";
            status += module.required_vermagic[0] != '\0'
                              ? std::string_view(module.required_vermagic)
                              : std::string_view("-");
            status += " versions_empty=";
            append_bool(status, module.versions_empty);
            status += " has_crcs=";
            append_bool(status, module.has_crcs);
            status += " signed=";
            append_bool(status, module.signed_module);
            status += " kcfi=";
            append_bool(status, module.kcfi_present);
            append_event(out, "diag.module_precheck", status);
        }
        {
            std::string_view status = "unknown";
            switch (report.outcome) {
                case DiagnosticOutcome::Ready: status = "ready"; break;
                case DiagnosticOutcome::DeviceBlocked: status = "device_blocked"; break;
                case DiagnosticOutcome::ModuleRejected: status = "module_rejected"; break;
            }
            append_event(out, "cve_2026_43284_diag", status);
        }
        return out;
    }

    int diagnostic_exit_code(const DiagnosticReport &report) noexcept {
        switch (report.outcome) {
            case DiagnosticOutcome::Ready: return 0;
            case DiagnosticOutcome::DeviceBlocked: return 2;
            case DiagnosticOutcome::ModuleRejected: return 3;
        }
        return 2;
    }

    int run_diagnostic_cli(std::string_view module_path) {
        if (module_path.empty()) {
            return 1;
        }
        const platform::DeviceProbeOps device = platform::real_device_probe();
        const DiagnosticReport report = run_device_diagnostic(device, module_path);
        const std::string text = format_diagnostic(report);
        if (!text.empty()) {
            (void)std::fwrite(text.data(), 1U, text.size(), stdout);
            (void)std::fflush(stdout);
        }
        return diagnostic_exit_code(report);
    }
} // namespace ghostlock::backend::cve_2026_43284::diagnostic
