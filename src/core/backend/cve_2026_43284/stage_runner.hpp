#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STAGE_RUNNER_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STAGE_RUNNER_HPP

/* B5-9c .ko plan/precheck/hook helpers for CVE-2026-43284.
 *
 * This unit used to own the staged device entry (--run-cve-2026-43284 <ko-path>
 * <target-file> --stage=plan|write|trigger|full). S4 R2b removed that entry with
 * the rest of the staged CLI surface, so what remains is the reusable core the
 * production path and the host tests share:
 *
 *   build_module_plan()        read the .ko and build the aligned write plan;
 *   precheck_staged_module()   fail-closed ELF/.modinfo/vermagic/__versions/
 *                              signature precheck of the exact bytes to write;
 *   reconcile_module_vermagic() optional in-place vermagic rewrite of the plan
 *                              mirror (used by run_stage(); the production path
 *                              deliberately does not call it -- see R2b);
 *   prepare_staged_hook()/plan_staged_hook()  read + arm the libc++ hook asset;
 *   run_stage()                the pure stage semantics over injected ChainOps.
 *
 * Consumers: execution_binding.cpp binds build_module_plan()/
 * precheck_staged_module()/prepare_staged_hook() for the production chain, and
 * cve_2026_43284_stage_runner_test.cpp drives run_stage() with fakes (no fork,
 * no write, no module load). The stage vocabulary and its structured report
 * stay because that is the shared diagnostic format, not a CLI contract.
 *
 * Stage semantics preserved by run_stage(): plan reads and prints without
 * writing; write verifies every block and never triggers; trigger fires the
 * double-fork sentry and polls the terminus; full additionally requires the
 * clean terminus. The report records carry paths, counts and enum names only;
 * SPI/ports/keys never reach a log. The dev-carrier relaxation now belongs to
 * the production --allow-dev-target switch (ChainRequest::allow_dev_carrier_path
 * and valid_dev_carrier_path()), not to this unit. */
#include "backend/cve_2026_43284/lkm/lkm_image.hpp"
#include "backend/cve_2026_43284/lkm/lkm_policy.hpp"
#include "backend/cve_2026_43284/real_ops.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"

#include <cstddef>
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
        /* B5-9h-1 hook-plan failures. */
        HookReadFailed,
        HookImageTooLarge,
        HookPlanFailed,
        /* B5-9h-4: hook target assets could not be wired to a usable page-cache
         * write face. The stage continues without the Hook binding and reports
         * hook_armed=0 rather than silently claiming success. */
        HookIoUnavailable,
        /* A countermeasure module failed to load/register (missing, bad
         * hash/path, ABI mismatch, reserved stage/capability). Fail-closed; the
         * LKM window never opens. The dev-only --plugin CLI flag that produced
         * it was removed in S4 R2b; the value stays reserved for the wire-carried
         * plugin.<id>.module_path selection (P1). */
        PluginRejected,
    };

    [[nodiscard]] std::string_view stage_error_name(StageError error) noexcept;

    /* Fail-closed staged-path .ko precheck (B5-4), called before patch #2:
     * run precheck_module_file against the kernel-required vermagic
     * (ELF/.modinfo name, same_magic() vermagic, signature). Never loads
     * anything. Returns false with the first failing rule. */
    [[nodiscard]] bool precheck_staged_module(std::string_view module_path,
                                              const lkm::DeviceKernelFacts &required,
                                              lkm::ModuleFacts &facts,
                                              lkm::LkmImageError &error) noexcept;

    /* B5-9h-3 fail-closed vermagic reconciliation over a caller-owned image
     * (the exact bytes the plan will write). The rewrite is limited to a
     * differing option tail (VermagicDiffReason::Options): when the image fails
     * the precheck solely for that reason and allow_rewrite is set, the
     * .modinfo entry is rewritten in place to the required value and the image
     * is re-prechecked. A release-token-only difference is never rewritten.
     * outcome reports original/required/rewritten; on every rejection the image
     * is left untouched and error carries the first failing rule. Never loads
     * or executes anything. */
    [[nodiscard]] bool reconcile_module_vermagic(std::uint8_t *image,
                                                 std::size_t image_size,
                                                 const lkm::DeviceKernelFacts &required,
                                                 bool allow_rewrite,
                                                 lkm::ModuleFacts &facts,
                                                 lkm::VermagicOutcome &outcome,
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

    /* ---- B5-9h-1 staged libc++ hook planning (read-only) ----
     *
     * plan_staged_hook() locates the hook symbol in a caller-owned image and
     * builds the parameterized shellcode/trampoline description. It only reads
     * the image through the injected read surface and never calls the write
     * surface, so a --stage=plan run writes no byte. The embedded upstream
     * libcxx.S template is used unless the caller overrides it. */
    struct StagedHookPlan final {
        bool attempted = false;
        bool valid = false;
        /* The displaced instruction is PC-relative (B/BL/ADR/ADRP/...): the
         * shellcode cannot replay it meaningfully at another address, so the
         * staged plan fails closed. This guard lives only in the staged plan;
         * the write primitive is unchanged. */
        bool steal_unsafe = false;
        StageError error = StageError::None;
        steps::HookPatchError hook_error = steps::HookPatchError::None;
        std::string_view target{};
        std::string_view symbol{};
        steps::HookGuardPolicy guard = steps::HookGuardPolicy::Skip;
        std::uint64_t hook_file_offset = 0U;
        std::uint64_t hook_vaddr = 0U;
        std::uint32_t displaced_instruction = 0U;
        std::uint32_t guard_instruction = 0U;
        bool guard_skipped = false;
        std::uint64_t shellcode_file_offset = 0U;
        std::uint64_t shellcode_vaddr = 0U;
        std::size_t shellcode_len = 0U;
        /* Mapped room at the landing site (BSS tail or executable page tail);
         * visible so a device plan run can be checked against the mapping. */
        std::uint64_t payload_max = 0U;
        std::uint64_t trampoline_offset = 0U;
        std::size_t trampoline_pos = 0U;
        std::size_t trampoline_len = steps::kHookTrampolineBytes;
    };

    [[nodiscard]] std::string_view hook_error_name(
            steps::HookPatchError error) noexcept;
    [[nodiscard]] std::string_view hook_guard_name(
            steps::HookGuardPolicy guard) noexcept;

    /* Everything plan_staged_hook() needs. image/io/buffers are caller-owned
     * and must outlive the call. A null tmpl selects the embedded upstream
     * libcxx.S template and binds carrier_path as the shellcode ko_target. */
    struct StagedHookRequest final {
        const std::uint8_t *image = nullptr;
        std::size_t image_size = 0U;
        std::string_view hook_target{};
        std::string_view symbol{};
        steps::HookGuardPolicy guard = steps::HookGuardPolicy::Skip;
        std::string_view carrier_path{};
        const steps::ShellcodeTemplate *tmpl = nullptr;
        const steps::ShellcodeBinding *bindings = nullptr;
        std::size_t binding_count = 0U;
        std::size_t displaced_slot = steps::kHookDisplacedNone;
        steps::HookPatchIo io{};
        std::uint8_t *shellcode_buf = nullptr;
        std::size_t shellcode_cap = 0U;
        std::uint8_t *shellcode_orig_buf = nullptr;
    };

    /* Fills out with the hook plan; returns out.valid. Failures are described
     * by out.error / out.hook_error and, for a PC-relative displaced
     * instruction, out.steal_unsafe. Read-only over the injected surface. */
    [[nodiscard]] bool plan_staged_hook(const StagedHookRequest &request,
                                        StagedHookPlan &out) noexcept;

    /* Reads a hook-target image into memory with a hard size cap. Returns false
     * with error == HookReadFailed (open/short read/empty) or
     * HookImageTooLarge (size > max_bytes). Only reads the file. */
    [[nodiscard]] bool read_hook_image(std::string_view path, std::size_t max_bytes,
                                       std::vector<std::uint8_t> &out,
                                       StageError &error);

    /* ---- B5-9h-4 hook application wiring (non-plan stages) ----
     *
     * prepare_staged_hook() reads the hook target image and arms a
     * RealChainContext with the image pointer, both caller-owned shellcode
     * buffers, the symbol/guard policy and the page-cache write face. The image
     * vector and the buffers must outlive the chain. ImageReadFailed /
     * ImageTooLarge are fatal; IoUnavailable leaves the context unarmed so the
     * caller must not bind apply_hook and must report it. */
    enum class StagedHookStatus : std::uint8_t {
        Armed = 0U,
        ImageReadFailed,
        ImageTooLarge,
        IoUnavailable,
    };

    struct StagedHookAssets final {
        StagedHookStatus status = StagedHookStatus::ImageReadFailed;
        StageError error = StageError::HookReadFailed;
    };

    [[nodiscard]] StagedHookAssets prepare_staged_hook(
            RealChainContext &ctx, std::string_view hook_target,
            std::string_view hook_symbol, steps::HookGuardPolicy guard,
            std::vector<std::uint8_t> &image, std::uint8_t *shellcode,
            std::size_t shellcode_cap, std::uint8_t *shellcode_orig,
            const steps::HookPatchIo &io);

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
        /* B5-9h-1 hook plan diagnostics. attempted stays false for a stage that
         * did not plan a hook, so the run.hook record is omitted. */
        StagedHookPlan hook{};
        /* B5-9h-4 hook application diagnostics for the non-plan stages:
         * hook_planned is set once a hook target was read, hook_armed is true
         * only when every asset (image + shellcode buffers + page-cache write
         * face) is bound, and hook_error names the first reason it is not.
         * Whether it was actually applied stays in chain.hook_applied. */
        bool hook_planned = false;
        bool hook_armed = false;
        StageError hook_error = StageError::None;
        /* B5-9h-3 vermagic reconciliation outcome for the run.module record. */
        lkm::VermagicOutcome vermagic = lkm::VermagicOutcome::Unchecked;
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


} // namespace ghostlock::backend::cve_2026_43284::stage_runner

#endif
