#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_BACKEND_TERMINAL_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_BACKEND_TERMINAL_HPP

/* B5-7: CVE-2026-43284 backend -> umh_forward terminal bridge (contract layer).
 *
 * This module is the only place that composes the 43284 endgame: it collects
 * the device facts, resolves the profile tokens (LKM selection, late-load UMH
 * argv, vendor carrier), runs the B5-6 chain through injected ops and, once the
 * LKM/UMH terminus is observed, fills terminal::UmhForwardInput for the
 * umh_forward terminal (execution lands in B6/T5).
 *
 * Every device interaction is injected: platform::DeviceProbeOps supplies the
 * firmware facts and steps::ChainOps supplies the page-cache write/read,
 * trigger, wait and release. The production bindings land in B5-8/B5-9; this
 * unit never forks, execs or writes a file, and the host tests drive it with
 * fakes. backend -> {terminal, platform, contract} is the allowed edge
 * (ADR-0004 R1); pipeline/ is not included here.
 *
 * The profile value shape is cve_2026_43284_state.hpp; the injected surface and
 * the run summary are below. All policy failures are fail-closed: the output
 * UmhForwardInput is written only after a clean terminus (lkm_loaded). */

#include "backend/cve_2026_43284/ipsec/ipsec.hpp"
#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"
#include "backend/cve_2026_43284_state.hpp"
#include "platform/device_facts.hpp"
#include "session/core_session.hpp"
#include "terminal/terminal_input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284 {

    /* Single-candidate carrier policy (B6/T5, stringified S4 R4). Exactly one
     * candidate is chosen:
     *   - a non-empty explicit carrier path selects that path (validated with
     *     steps::valid_carrier_path; an invalid one fails closed);
     *   - an empty/absent path selects the first kDefaultCarriers entry the
     *     device probe reports present, else the first default (the probe is
     *     best-effort; the chain fails closed later if the choice is unusable).
     * On success `out` aliases either the caller explicit path (which must
     * outlive the run) or a static kDefaultCarriers entry; nothing is written.
     * Returns false when no candidate is usable. */
    [[nodiscard]] bool select_single_carrier(
            std::string_view path,
            const platform::DeviceProbeOps &device,
            steps::CarrierTarget &out) noexcept;

    /* Injected dependencies. `device` and `chain` are the ways the
     * orchestration reaches a device; both default to an unavailable surface so
     * a run with no bindings fails closed with a diagnostic. The single carrier
     * and the module plan are composition-root-owned and must outlive the run. */
    struct BackendTerminalDeps final {
        platform::DeviceProbeOps device{};
        steps::ChainOps chain{};
        /* Optional .ko precheck. Skipped when null or when lkm_image_path is
         * empty. `precheck_ctx` is passed as the first argument. */
        bool (*precheck_lkm)(void *ctx, std::string_view path,
                             const lkm::DeviceKernelFacts &required,
                             lkm::ModuleFacts &facts,
                             lkm::LkmImageError &error) noexcept = nullptr;
        void *precheck_ctx = nullptr;
        /* Path handed to the optional precheck (empty means skip). */
        std::string_view lkm_image_path{};
        /* Chain wait budget, forwarded to ChainRequest::wait_timeout_ms. */
        std::uint32_t wait_timeout_ms = 5000U;
        /* B6/T5: exactly one composition-root-selected carrier. run_backend_
         * terminal binds this candidate and never falls back; a null/empty
         * carrier fails closed (CarrierRejected) before patch #1 or any write.
         * The pointer must outlive the run; it normally names the state's
         * single-carrier slot, itself pointing into static kDefaultCarriers. */
        const steps::CarrierTarget *carrier = nullptr;
        /* Composition-root-built module write plan. When ChainOps::build_plan is
         * null the chain uses this pre-computed plan; when both are null the
         * empty plan fails closed (InvalidPlan) before patch #1 / hook /
         * trigger. The region bytes must outlive the run. */
        const steps::PatchPlan *plan = nullptr;
        /* Target-file size from fstat(2) on the already-opened carrier fd
         * (0 == unknown). Forwarded to ChainRequest::target_size so the closure
         * check rejects an out-of-bounds region before any byte is written. */
        std::uint64_t target_size = 0U;
        /* B5-8: the umh_forward terminal's read-only readiness handle. Copied
         * into UmhForwardInput::channel once the terminus is clean; the default
         * (invalid) handle makes the terminal fail closed until the composition
         * root binds the production probe. */
        terminal::UmhForwardChannel umh_channel{};
    };

    enum class BackendTerminalError : std::uint8_t {
        None = 0,
        StepsMismatch,        /* profile.steps absent or not PageCacheWrite */
        ProfileIncomplete,    /* a mandatory 43284 token is absent */
        DeviceFactsUnavailable,
        DeviceFactsIncomplete,
        PatchedKernel,        /* f4c50a4 already applied: not applicable */
        LkmPolicyRejected,
        LkmPrecheckRejected,
        UmhCommandRejected,
        CarrierRejected,
        ChainRejected,
    };

    struct BackendTerminalResult final {
        BackendTerminalError error = BackendTerminalError::None;
        platform::DeviceFactError fact_error = platform::DeviceFactError::None;
        /* Facts a denied probe could not read, carried for the production log
         * and the device gate even when the run succeeds. See
         * platform::DeviceFactDegraded. */
        platform::DeviceFactDegraded degraded = platform::DeviceFactDegraded::None;
        lkm::LkmPolicyError lkm_error = lkm::LkmPolicyError::None;
        lkm::LkmImageError image_error = lkm::LkmImageError::None;
        lkm::UmhCommandError command_error = lkm::UmhCommandError::None;
        steps::ChainResult chain{};
        /* True only after the chain reached the LKM/UMH terminus and the output
         * UmhForwardInput was filled. */
        bool ready = false;
    };

    /* Runs the injected endgame. `sa` is borrowed, not copied: on success the
     * output references it as session_secrets. `force_attack` is accepted for
     * the BackendExecution signature but never bypasses a fail-closed fact.
     * `out` is left untouched unless the return value is ready. */
    [[nodiscard]] BackendTerminalResult run_backend_terminal(
            const Cve2026_43284Profile &profile,
            const terminal::RootProgram &root_program,
            const IpsecSaParams &sa,
            const BackendTerminalDeps &deps,
            bool force_attack,
            terminal::UmhForwardInput &out) noexcept;

    /* Backend-private execution state in the shared CoreSession slot (the same
     * slot type 43499 uses; never both at once). It carries the decoded 43284
     * profile, the App root program, the session secrets and the injected ops.
     * The device/chain ops are populated by the composition root in B5-8; the
     * defaults are unavailable, so a premature run fails closed. */
    struct Cve2026_43284State final {
        Cve2026_43284Profile profile{};
        terminal::RootProgram root_program{};
        IpsecSaParams sa{};
        BackendTerminalDeps deps{};
    };

    static_assert(sizeof(Cve2026_43284State) <= session::kBackendStateBytes,
                  "increase CoreSession::kBackendStateBytes");
    static_assert(alignof(Cve2026_43284State) <= session::kBackendStateAlign,
                  "increase CoreSession::kBackendStateAlign");

    /* Typed accessor; the only way the policy reaches its state. Precondition:
     * cve_2026_43284_state_construct() ran. */
    [[nodiscard]] Cve2026_43284State &cve_2026_43284_state(
            session::CoreSession &state) noexcept;
    [[nodiscard]] const Cve2026_43284State &cve_2026_43284_state(
            const session::CoreSession &state) noexcept;

    /* Idempotent in-place construction/destruction. The destructor zeroizes the
     * session secrets through zeroize(IpsecSaParams&). */
    void cve_2026_43284_state_construct(session::CoreSession &state) noexcept;
    void cve_2026_43284_state_destroy(session::CoreSession &state) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
