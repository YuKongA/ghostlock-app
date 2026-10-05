#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_EXECUTION_BINDING_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_EXECUTION_BINDING_HPP

/* Composition-root production binding for the CVE-2026-43284 backend (B6/T5).
 *
 * app-call -> orchestrator -> Pipeline already dispatches the catalogued
 * {43284, pagecache_write, umh_forward} triple, but the backend's per-run
 * resources are process/device facts that only the composition root can
 * assemble: the kernel-module mirror and its write plan, the single vendor
 * carrier, the real page-cache/crash_dump/libc++ chain context, the session
 * secrets and the read-only UMH readiness probe. bind_production_execution()
 * installs all of them into the session's 43284 state before Pipeline::run.
 *
 * Module source is the convention path $GHOSTLOCK_HOME/helper.ko (same class as
 * root_script_path/ksu_log_path). A future
 * backend.cve_2026_43284.module_path profile field can become the authority
 * without changing the wire.
 *
 * Fail-closed: a missing/unreadable module, a failing vermagic precheck, an
 * unresolved carrier or an unarmable libc++ hook leaves the state unbound, so
 * run_backend_terminal cannot reach patch #1 / hook / trigger.
 *
 * ProductionResources owns the buffers the state borrows and must outlive the
 * orchestrated pipeline call. */

#include "backend/cve_2026_43284/backend_terminal.hpp"
#include "backend/cve_2026_43284/ipsec/ipsec.hpp"
#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "backend/cve_2026_43284/stage_runner.hpp"
#include "profile/document.hpp"
#include "session/core_session.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::backend::cve_2026_43284 {

    enum class ExecutionBindError : std::uint8_t {
        None = 0,
        ModulePathEmpty,
        ModuleReadFailed,
        ModulePrecheckFailed,
        CarrierRejected,
        TargetUnavailable,
        HookUnavailable,
    };

    struct ExecutionBindResult final {
        ExecutionBindError error = ExecutionBindError::None;
        stage_runner::StageError plan_error = stage_runner::StageError::None;
        lkm::LkmImageError image_error = lkm::LkmImageError::None;
    };

    /* Owns every per-run resource the 43284 state borrows. Kept alive by the
     * composition root across the orchestrated pipeline call; the module plan
     * aliases module.bytes and the chain ops alias chain.page, so the object
     * must not move once bound. */
    struct ProductionResources final {
        stage_runner::PlanBuffer module{};
        RealChainContext chain{};
        steps::CarrierTarget carrier{};
        /* Owns the convention module path so the state's lkm_image_path view
         * stays valid across the run without adding a RuntimeConfig field
         * (which would shift the CoreSession/43499 layout). */
        std::string module_path{};
        std::vector<std::uint8_t> hook_image{};
        std::array<std::uint8_t, steps::kShellcodeMaxBytes> hook_shellcode{};
        std::array<std::uint8_t, steps::kShellcodeMaxBytes> hook_shellcode_orig{};
    };

    /* precheck adapter matching BackendTerminalDeps::precheck_lkm. */
    [[nodiscard]] bool production_module_precheck(
            void *ctx, std::string_view path,
            const lkm::DeviceKernelFacts &required, lkm::ModuleFacts &facts,
            lkm::LkmImageError &error) noexcept;

    /* Test/composition seam: caller supplies the device probe and module path.
     * The production entry point below uses runtime_config_snapshot() and
     * platform::real_device_probe(). */
    [[nodiscard]] ExecutionBindResult bind_production_execution_with(
            session::CoreSession &session, ProductionResources &resources,
            const profile::Document &document, const IpsecSaParams &sa,
            std::string_view module_path,
            const platform::DeviceProbeOps &device);

    /* Production entry point. Reads $GHOSTLOCK_HOME/helper.ko, builds the
     * aligned single-region write plan, selects/binds the one carrier, arms the
     * real chain context and installs everything into the 43284 state. */
    [[nodiscard]] ExecutionBindResult bind_production_execution(
            session::CoreSession &session, ProductionResources &resources,
            const profile::Document &document, const IpsecSaParams &sa);

} // namespace ghostlock::backend::cve_2026_43284

#endif
