#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STAGE_RUNNER_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STAGE_RUNNER_HPP

/* B5-9c staged execution entry for CVE-2026-43284.
 *
 * An explicit operator flag (--run-cve-2026-43284 <ko-path> <target-file>
 * --stage=plan|write|trigger|full) drives one verifiable step of the endgame so
 * the stage can be validated on a real device in isolation:
 *
 *   plan     read the .ko, build the 16-byte-aligned write plan and print it;
 *            no file is written and no device op is bound;
 *   write    write + read-back verify every planned block, never trigger;
 *   trigger  write + verify + fire the double-fork sentry and poll the LKM/UMH
 *            terminus; the terminus outcome is reported but does not fail the
 *            stage (use full when readiness must be proven);
 *   full     the complete chain; success requires the clean terminus
 *            (lkm_loaded) in addition to a verified write.
 *
 * A dev-only --allow-dev-target flag additionally lets the staged entry accept
 * a non-vendor one-shot target (for example under /data/local/tmp) so the write
 * primitive can be validated on a disposable file. It relaxes only this staged
 * entry through ChainRequest::allow_dev_carrier_path: valid_carrier_path(),
 * build_carrier_list() and the default pipeline keep the /vendor constraint,
 * and the records mark the mode as run.dev_target allow=1 to prevent misuse.
 *
 * Every stage emits structured, 0x1e-framed status records in the same style as
 * --enable-status-record. The records carry paths, counts and enum names only;
 * SPI/ports/keys never reach a log. The staged entry is not part of any default
 * pipeline selection: backend_available(Cve2026_43284) and
 * selection_supported() stay false, and without the explicit flag nothing here
 * is reachable.
 *
 * run_stage() is pure over injected ChainOps, so host tests exercise the stage
 * semantics and the failure propagation with fakes and never fork or write.
 * run_stage_cli() is the device entry that binds the real ops. */

#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::backend::cve_2026_43284::stage_runner {

    enum class Stage : std::uint8_t {
        Plan = 0U,
        Write,
        Trigger,
        Full,
    };

    [[nodiscard]] std::string_view stage_name(Stage stage) noexcept;
    /* Parses plan/write/trigger/full; any other spelling returns false. */
    [[nodiscard]] bool parse_stage(std::string_view text, Stage &out) noexcept;

    enum class StageError : std::uint8_t {
        None = 0U,
        InvalidArgument,
        ModuleReadFailed,
        ModulePrecheckFailed,
        PlanInvalid,
        /* The patch plan reaches past the known target-file size. */
        TargetOutOfBounds,
        /* A declared pre-image did not match the target's current content. */
        PreImageMismatch,
        NotReady,
        SessionRejected,
        WriteRejected,
        TriggerRejected,
        WaitRejected,
        /* The ksud failure marker (/dev/dfm1) was observed. Upstream rc 1. */
        KsudFailed,
        CleanupRejected,
    };

    [[nodiscard]] std::string_view stage_error_name(StageError error) noexcept;

    /* Fail-closed staged-path .ko precheck (B5-4), called before patch #2:
     * parse the release (uname -r / canonical KMI) and run precheck_module_file
     * (ELF/.modinfo name/vermagic/__versions/signature). Never loads anything.
     * Returns false with the first failing rule. */
    [[nodiscard]] bool precheck_staged_module(std::string_view module_path,
                                              std::string_view release,
                                              lkm::ModuleFacts &facts,
                                              lkm::LkmImageError &error) noexcept;

    /* Caller-owned module image plus the patch plan that points into it. The
     * region aliases the bytes vector; the buffer must not move after setup. */
    struct PlanBuffer final {
        std::vector<std::uint8_t> bytes{};
        steps::PatchRegion region{};
        steps::PatchPlan plan{};
        std::uint32_t blocks = 0U;
        std::uint64_t module_bytes = 0U;
    };

    /* Reads the module, zero-pads it to a 16-byte multiple and builds a
     * single-region plan that stores it at offset 0 of the target. Only reads
     * the module file; never touches the target. */
    [[nodiscard]] bool build_module_plan(std::string_view module_path,
                                         PlanBuffer &out, StageError &error);

    struct StageReport final {
        Stage stage = Stage::Plan;
        StageError error = StageError::None;
        steps::ChainResult chain{};
        bool wrote = false;
        bool verified = false;
        bool triggered = false;
        bool terminus = false;
        /* Trigger stage: the sentry fired but the terminus was not observed. */
        bool wait_incomplete = false;
        /* True when the request carried the dev-only non-vendor carrier escape
         * hatch; echoed in the structured records as a misuse guard. */
        bool dev_target = false;
        /* Diagnostics carried into the run.target record: the target-file size
         * used for the boundary assertion (0 == unknown), the plan span
         * [plan_offset, plan_offset + plan_len) and whether a declared
         * pre-image was verified (preimage=ok) or none was asked for
         * (preimage=absent). */
        std::uint64_t target_size = 0U;
        std::uint64_t plan_offset = 0U;
        std::uint64_t plan_len = 0U;
        bool preimage_ok = false;
    };

    /* Pure stage semantics over injected ChainOps. For Plan it only validates
     * the request plan; for Write it runs the chain with ChainStopAfter::Write;
     * for Trigger/Full it runs the complete chain and applies the stage-specific
     * success rule. Never dereferences a device by itself. */
    [[nodiscard]] StageReport run_stage(Stage stage,
                                        const steps::ChainRequest &request,
                                        const steps::ChainOps &ops,
                                        steps::ChainWorkspace &workspace) noexcept;

    /* 0x1e-framed structured records. module_bytes is the unpadded module size
     * (0 when unknown). Never emits secrets. */
    [[nodiscard]] std::string format_stage_report(const StageReport &report,
                                                  std::string_view module_path,
                                                  std::string_view target_path,
                                                  std::uint64_t module_bytes);

    /* plan/write keep the staged codes: 0 None, 1 usage, 2 unavailable/
     * rejected, 3 write, 4 trigger, 5 wait, 6 cleanup. trigger/full are aligned
     * with the upstream nativeRunAll rc semantics: 0 success, 1 ksud failed,
     * 2 timeout, 3 patch/write/trigger/cleanup failure. */
    [[nodiscard]] int stage_exit_code(const StageReport &report) noexcept;

    /* Device entry: build the plan, read the optional session-secret frame from
     * stdin (write stages), open the target, bind make_real_chain_ops() and run
     * one stage. Prints the structured records. Not noexcept (allocation).
     * allow_dev_target is the explicit --allow-dev-target escape hatch: it is
     * forwarded to ChainRequest::allow_dev_carrier_path and echoed as
     * run.dev_target; the default false keeps the /vendor-only carrier rule. */
    int run_stage_cli(std::string_view module_path, std::string_view target_path,
                      Stage stage, bool allow_dev_target);

} // namespace ghostlock::backend::cve_2026_43284::stage_runner

#endif
