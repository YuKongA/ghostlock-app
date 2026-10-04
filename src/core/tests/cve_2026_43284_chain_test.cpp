/* Host tests for B5-6: the CVE-2026-43284 endgame chain.
 *
 * The page-cache write (B5-3), read-back, trigger and wait are all replaced by
 * a fake ChainOps, so the tests observe the orchestration semantics (stage
 * ordering, carrier fallback, error propagation, best-effort rollback and the
 * single terminus) rather than device behaviour. No file is written, no
 * process is forked and no syscall is issued; the test links the same chain
 * translation unit the device build uses (chain.cpp).
 *
 * Covered:
 *   - stage order: read(journal) -> write -> read(verify), trigger, wait,
 *     release;
 *   - carrier fallback on an unusable carrier and fail-closed when every
 *     candidate is unusable;
 *   - write failure propagation and rollback of the blocks written so far;
 *   - verify-mismatch detection and rollback;
 *   - trigger failure and every wait outcome;
 *   - no-carrier / not-available / invalid-plan early returns still run the
 *     terminus;
 *   - the build_plan injection point;
 *   - journal overflow reporting. */

#include "backend/cve_2026_43284/steps/chain.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

    using ghostlock::backend::cve_2026_43284::steps::build_carrier_list;
    using ghostlock::backend::cve_2026_43284::steps::CarrierList;
    using ghostlock::backend::cve_2026_43284::steps::CarrierTarget;
    using ghostlock::backend::cve_2026_43284::steps::ChainError;
    using ghostlock::backend::cve_2026_43284::steps::ChainOps;
    using ghostlock::backend::cve_2026_43284::steps::ChainRequest;
    using ghostlock::backend::cve_2026_43284::steps::ChainResult;
    using ghostlock::backend::cve_2026_43284::steps::ChainWaitOutcome;
    using ghostlock::backend::cve_2026_43284::steps::ChainWorkspace;
    using ghostlock::backend::cve_2026_43284::steps::kChainBlockBytes;
    using ghostlock::backend::cve_2026_43284::steps::kChainRollbackBlocks;
    using ghostlock::backend::cve_2026_43284::steps::kDefaultCarriers;
    using ghostlock::backend::cve_2026_43284::steps::PatchPlan;
    using ghostlock::backend::cve_2026_43284::steps::PatchRegion;
    using ghostlock::backend::cve_2026_43284::steps::run_chain;
    using ghostlock::backend::cve_2026_43284::steps::valid_carrier_path;
    using ghostlock::backend::cve_2026_43284::steps::valid_dev_carrier_path;

    enum class Op : std::uint8_t {
        BuildPlan = 0U,
        CrashDump,
        Read,
        Write,
        Hook,
        Trigger,
        Wait,
        Restore,
        Release,
    };

    struct Fake final {
        std::array<std::uint8_t, 4096> file{};
        std::array<Op, 1024> events{};
        std::size_t event_count = 0U;

        int read_fail_at = -1;
        bool read_fail_always = false;
        int write_fail_at = -1;
        int corrupt_write_at = -1;
        bool trigger_fail = false;
        ChainWaitOutcome wait_outcome = ChainWaitOutcome::LkmLoaded;
        std::uint32_t wait_timeout_seen = 0U;

        int read_calls = 0;
        int write_calls = 0;
        std::uint32_t release_calls = 0U;
        ChainError patch_crash_dump_error = ChainError::None;
        ChainError apply_hook_error = ChainError::None;
        std::array<std::uint8_t, 32> secret{};

        PatchPlan planned_plan{};
    };

    Fake g;

    void record(Op op) noexcept {
        if (g.event_count < g.events.size()) {
            g.events[g.event_count] = op;
            ++g.event_count;
        }
    }

    bool events_are(std::initializer_list<Op> expected) noexcept {
        if (g.event_count != expected.size()) {
            return false;
        }
        std::size_t i = 0U;
        for (const Op op : expected) {
            if (g.events[i] != op) {
                return false;
            }
            ++i;
        }
        return true;
    }

    bool events_end_with(std::initializer_list<Op> expected) noexcept {
        if (g.event_count < expected.size()) {
            return false;
        }
        std::size_t i = g.event_count - expected.size();
        for (const Op op : expected) {
            if (g.events[i] != op) {
                return false;
            }
            ++i;
        }
        return true;
    }

    bool fake_build_plan(void *ctx, PatchPlan &out, ChainError &error) noexcept {
        (void)ctx;
        (void)error;
        record(Op::BuildPlan);
        out = g.planned_plan;
        return true;
    }

    long fake_read_block(void *ctx, std::uint64_t offset, std::uint8_t out[16]) noexcept {
        (void)ctx;
        record(Op::Read);
        ++g.read_calls;
        if (g.read_fail_always) {
            return -5; /* EIO */
        }
        if (g.read_fail_at > 0 && g.read_calls == g.read_fail_at) {
            return -5;
        }
        for (std::size_t i = 0U; i < kChainBlockBytes; ++i) {
            out[i] = g.file[static_cast<std::size_t>(offset) + i];
        }
        return static_cast<long>(kChainBlockBytes);
    }

    std::int32_t fake_write16(void *ctx, std::uint64_t offset, const void *bytes16) noexcept {
        (void)ctx;
        record(Op::Write);
        ++g.write_calls;
        if (g.write_fail_at > 0 && g.write_calls == g.write_fail_at) {
            return 1;
        }
        const auto *src = static_cast<const std::uint8_t *>(bytes16);
        bool corrupt = false;
        if (g.corrupt_write_at > 0 && g.write_calls == g.corrupt_write_at) {
            corrupt = true;
            g.corrupt_write_at = -1; /* one-shot: rollback writes stay clean */
        }
        for (std::size_t i = 0U; i < kChainBlockBytes; ++i) {
            const std::uint8_t value =
                    corrupt ? static_cast<std::uint8_t>(src[i] ^ 0xFFU) : src[i];
            g.file[static_cast<std::size_t>(offset) + i] = value;
        }
        return 0;
    }

    int fake_trigger(void *ctx) noexcept {
        (void)ctx;
        record(Op::Trigger);
        return g.trigger_fail ? 1 : 0;
    }

    ChainWaitOutcome fake_wait(void *ctx, std::uint32_t timeout_ms) noexcept {
        (void)ctx;
        record(Op::Wait);
        g.wait_timeout_seen = timeout_ms;
        return g.wait_outcome;
    }

    void fake_release(void *ctx) noexcept {
        (void)ctx;
        record(Op::Release);
        ++g.release_calls;
        for (std::size_t i = 0U; i < g.secret.size(); ++i) {
            g.secret[i] = 0U;
        }
    }

    ChainError fake_patch_crash_dump(void *ctx) noexcept {
        (void)ctx;
        record(Op::CrashDump);
        return g.patch_crash_dump_error;
    }

    ChainError fake_apply_hook(void *ctx) noexcept {
        (void)ctx;
        record(Op::Hook);
        return g.apply_hook_error;
    }

    void fake_restore_hook(void *ctx) noexcept {
        (void)ctx;
        record(Op::Restore);
    }

    ChainOps make_ops() noexcept {
        ChainOps ops{};
        ops.write.ctx = &g;
        ops.write.write16 = fake_write16;
        ops.read_block = fake_read_block;
        ops.trigger = fake_trigger;
        ops.wait_result = fake_wait;
        ops.release = fake_release;
        return ops;
    }

    ChainOps make_ops_full() noexcept {
        ChainOps ops = make_ops();
        ops.patch_crash_dump = fake_patch_crash_dump;
        ops.apply_hook = fake_apply_hook;
        ops.restore_hook = fake_restore_hook;
        return ops;
    }

    void reset() noexcept {
        g = Fake{};
    }

    /* ---- path validation and candidate-list building ---- */

    void test_carrier_paths() {
        reset();
        assert(valid_carrier_path("/vendor/lib64/libbinderdebug.so"));
        assert(valid_carrier_path("/system/vendor/lib64/libx.so"));
        assert(!valid_carrier_path(""));
        assert(!valid_carrier_path("vendor/lib64/libx.so"));
        assert(!valid_carrier_path("/system/lib64/libc++.so"));
        assert(!valid_carrier_path("/vendor/lib64/bad path.so"));
        assert(!valid_carrier_path("/vendor/lib64/bad\tname.so"));

        /* Dev-only relaxation: the same structural checks without the
         * /vendor prefix. Relative, empty, whitespace-bearing and over-long
         * paths stay rejected. */
        assert(valid_dev_carrier_path("/data/local/tmp/ghostlock-dev.bin"));
        assert(valid_dev_carrier_path("/vendor/lib64/libbinderdebug.so"));
        assert(valid_dev_carrier_path("/system/lib64/libc++.so"));
        assert(!valid_dev_carrier_path(""));
        assert(!valid_dev_carrier_path("data/local/tmp/x"));
        assert(!valid_dev_carrier_path("/data/local/tmp/bad path.so"));
        assert(!valid_dev_carrier_path("/data/local/tmp/bad\tname.so"));
        assert(!valid_dev_carrier_path(
                "/data/local/tmp/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin"));

        CarrierTarget only[2] = {
            {"/vendor/lib64/primary.so", 4096U},
            {"/system/vendor/lib64/primary2.so", 0U},
        };
        CarrierList list{};
        assert(build_carrier_list(only, 2U, list));
        assert(list.count == 2U + kDefaultCarriers.size());
        assert(list.items[0].path == "/vendor/lib64/primary.so");
        assert(list.items[0].size == 4096U);
        assert(list.items[1].path == "/system/vendor/lib64/primary2.so");
        assert(list.items[2].path == kDefaultCarriers[0].path);

        /* Duplicate primary is kept once and invalid entries are dropped. */
        CarrierTarget dup[3] = {
            {"/vendor/lib64/dup.so", 0U},
            {"/vendor/lib64/dup.so", 0U},
            {"not/absolute.so", 0U},
        };
        CarrierList dedup{};
        assert(build_carrier_list(dup, 3U, dedup));
        assert(dedup.count == 1U + kDefaultCarriers.size());
        assert(dedup.items[0].path == "/vendor/lib64/dup.so");

        CarrierList empty{};
        assert(build_carrier_list(nullptr, 0U, empty));
        assert(empty.count == kDefaultCarriers.size());
    }

    /* ---- dev-only staged-run hatch: non-vendor carrier accepted only when
     *      the request opts in; the default stays fail-closed ---- */

    void test_dev_carrier_escape() {
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x5AU);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "dev"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/data/local/tmp/ghostlock-dev.bin", 0U}};

        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        ChainResult result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::NoCarrier);
        assert(result.blocks_written == 0U);
        assert(g.write_calls == 0);
        assert(g.release_calls == 1U);
        assert(workspace.carrier_attempt_count == 1U);
        assert(workspace.carrier_attempts[0].error == ChainError::CarrierUnusable);

        reset();
        request.allow_dev_carrier_path = true;
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::None);
        assert(result.blocks_written == 1U);
        assert(result.blocks_verified == 1U);
        assert(result.carrier == &carriers[0]);
        assert(result.cleanup_ran);
        assert(g.write_calls == 1);
        assert(g.release_calls == 1U);
        for (std::size_t i = 0U; i < payload.size(); ++i) {
            assert(g.file[i] == 0x5AU);
        }
    }

    /* ---- happy path: stage order, per-block verify, terminus ---- */

    void test_success_order() {
        reset();
        std::array<std::uint8_t, 32> payload_a{};
        std::array<std::uint8_t, 16> payload_b{};
        for (std::size_t i = 0U; i < payload_a.size(); ++i) {
            payload_a[i] = static_cast<std::uint8_t>(i + 1U);
        }
        for (std::size_t i = 0U; i < payload_b.size(); ++i) {
            payload_b[i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        PatchRegion regions[2] = {
            {0U, payload_a.data(), payload_a.size(), true, true, "a"},
            {64U, payload_b.data(), payload_b.size(), true, true, "b"},
        };
        PatchPlan plan{regions, 2U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/libbinderdebug.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;
        request.wait_timeout_ms = 1234U;

        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::None);
        assert(result.lkm_loaded);
        assert(result.cleanup_ran);
        assert(result.blocks_written == 3U);
        assert(result.blocks_verified == 3U);
        assert(result.blocks_rolled_back == 0U);
        assert(result.carrier == &carriers[0]);
        assert(result.wait == ChainWaitOutcome::LkmLoaded);
        assert(g.wait_timeout_seen == 1234U);
        assert(g.release_calls == 1U);
        assert(workspace.carrier_attempt_count == 1U);
        assert(workspace.carrier_attempts[0].error == ChainError::None);
        assert(workspace.carrier_attempts[0].blocks_written == 3U);
        assert(events_are({Op::Read, Op::Write, Op::Read,
                           Op::Read, Op::Write, Op::Read,
                           Op::Read, Op::Write, Op::Read,
                           Op::Trigger, Op::Wait, Op::Release}));
        for (std::size_t i = 0U; i < payload_a.size(); ++i) {
            assert(g.file[i] == payload_a[i]);
        }
        for (std::size_t i = 0U; i < payload_b.size(); ++i) {
            assert(g.file[64U + i] == payload_b[i]);
        }
        assert(g.file[32U] == 0U);
    }

    /* ---- patch #1 / hook ordering, failure propagation and restore. ---- */

    void test_hook_stage_order() {
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x5AU);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "h"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/libbinderdebug.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        /* success: patch #1 -> carrier write/verify -> hook -> trigger -> wait
         * -> restore -> release. */
        reset();
        {
            ChainOps ops = make_ops_full();
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            assert(result.error == ChainError::None);
            assert(result.crash_dump_patched);
            assert(result.hook_applied);
            assert(result.hook_restored);
            assert(events_are({Op::CrashDump, Op::Read, Op::Write, Op::Read,
                               Op::Hook, Op::Trigger, Op::Wait, Op::Restore,
                               Op::Release}));
        }

        /* patch #1 failure: no carrier write, no hook, but restore + release
         * still run (unconditional terminus). */
        reset();
        g.patch_crash_dump_error = ChainError::CrashDumpFailed;
        {
            ChainOps ops = make_ops_full();
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            assert(result.error == ChainError::CrashDumpFailed);
            assert(!result.crash_dump_patched);
            assert(!result.hook_applied);
            assert(!result.lkm_loaded);
            assert(g.release_calls == 1U);
            assert(events_are({Op::CrashDump, Op::Restore, Op::Release}));
        }

        /* hook apply failure: the carrier write happened, no trigger, and the
         * terminus still restores and releases. */
        reset();
        g.apply_hook_error = ChainError::HookFailed;
        {
            ChainOps ops = make_ops_full();
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            assert(result.error == ChainError::HookFailed);
            assert(result.crash_dump_patched);
            assert(!result.hook_applied);
            assert(result.hook_restored);
            assert(events_are({Op::CrashDump, Op::Read, Op::Write, Op::Read,
                               Op::Hook, Op::Restore, Op::Release}));
        }

        /* the dev-only escape hatch must not patch crash_dump64. */
        reset();
        {
            ChainRequest dev = request;
            dev.allow_dev_carrier_path = true;
            CarrierTarget dev_carriers[1] = {{"/data/local/tmp/x.bin", 0U}};
            dev.carriers = dev_carriers;
            ChainOps ops = make_ops_full();
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(dev, ops, workspace);
            assert(result.error == ChainError::None);
            assert(!result.crash_dump_patched);
            assert(result.hook_applied);
            assert(events_are({Op::Read, Op::Write, Op::Read, Op::Hook,
                               Op::Trigger, Op::Wait, Op::Restore, Op::Release}));
        }
    }

    /* ---- carrier fallback: first unusable, second succeeds ---- */

    void test_carrier_fallback() {
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x5AU);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "b"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[2] = {
            {"/vendor/lib64/first.so", 0U},
            {"/vendor/lib64/second.so", 0U},
        };
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 2U;
        request.plan = plan;

        g.read_fail_at = 1; /* first carrier's first journal read fails */
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::None);
        assert(result.carrier == &carriers[1]);
        assert(result.blocks_written == 1U);
        assert(result.blocks_verified == 1U);
        assert(result.cleanup_ran);
        assert(workspace.carrier_attempt_count == 2U);
        assert(workspace.carrier_attempts[0].error == ChainError::CarrierUnusable);
        assert(workspace.carrier_attempts[0].blocks_written == 0U);
        assert(workspace.carrier_attempts[1].error == ChainError::None);
        assert(g.release_calls == 1U);
        assert(events_are({Op::Read,
                           Op::Read, Op::Write, Op::Read,
                           Op::Trigger, Op::Wait, Op::Release}));
    }

    /* ---- every candidate unusable -> fail-closed, nothing written ---- */

    void test_all_carriers_unusable() {
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x11U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "b"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[2] = {
            {"/vendor/lib64/a.so", 0U},
            {"/vendor/lib64/b.so", 0U},
        };
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 2U;
        request.plan = plan;

        g.read_fail_always = true;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::NoCarrier);
        assert(result.carrier == nullptr);
        assert(!result.lkm_loaded);
        assert(result.blocks_written == 0U);
        assert(result.cleanup_ran);
        assert(g.write_calls == 0);
        assert(g.release_calls == 1U);
        assert(workspace.carrier_attempt_count == 2U);
        assert(workspace.carrier_attempts[0].error == ChainError::CarrierUnusable);
        assert(workspace.carrier_attempts[1].error == ChainError::CarrierUnusable);
        assert(events_are({Op::Read, Op::Read, Op::Release}));
    }

    /* ---- write failure mid-span -> rollback the written blocks ---- */

    void test_write_failure_rollback() {
        reset();
        std::array<std::uint8_t, 32> payload{};
        payload.fill(0x77U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "w"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        g.file.fill(0x22U);
        g.write_fail_at = 2;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::WriteFailed);
        assert(result.blocks_written == 1U);
        assert(result.blocks_verified == 1U);
        assert(result.blocks_rolled_back == 2U);
        assert(!result.lkm_loaded);
        assert(result.cleanup_ran);
        assert(g.release_calls == 1U);
        assert(events_are({Op::Read, Op::Write, Op::Read,
                           Op::Read, Op::Write,
                           Op::Write, Op::Write,
                           Op::Release}));
        for (std::size_t i = 0U; i < payload.size(); ++i) {
            assert(g.file[i] == 0x22U);
        }
    }

    /* ---- verify mismatch -> rollback and stop before the trigger ---- */

    void test_verify_mismatch_rollback() {
        reset();
        std::array<std::uint8_t, 32> payload{};
        payload.fill(0x33U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "v"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        g.file.fill(0x44U);
        g.corrupt_write_at = 1;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::VerifyMismatch);
        assert(result.blocks_written == 1U);
        assert(result.blocks_verified == 0U);
        assert(result.blocks_rolled_back == 1U);
        assert(result.cleanup_ran);
        assert(!events_end_with({Op::Trigger}));
        assert(events_are({Op::Read, Op::Write, Op::Read, Op::Write, Op::Release}));
        for (std::size_t i = 0U; i < payload.size(); ++i) {
            assert(g.file[i] == 0x44U);
        }
    }

    /* ---- trigger failure: writes are kept, wait is skipped, terminus runs ---- */

    void test_trigger_failure() {
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x66U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "t"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        g.trigger_fail = true;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::TriggerFailed);
        assert(result.blocks_written == 1U);
        assert(result.cleanup_ran);
        assert(g.release_calls == 1U);
        assert(events_are({Op::Read, Op::Write, Op::Read, Op::Trigger, Op::Release}));
    }

    /* ---- every wait outcome is mapped, release always runs ---- */

    void test_wait_outcomes() {
        struct Case final {
            ChainWaitOutcome outcome;
            ChainError expected;
            bool loaded;
        };
        const Case cases[4] = {
            {ChainWaitOutcome::LkmLoaded, ChainError::None, true},
            {ChainWaitOutcome::Failed, ChainError::LkmFailed, false},
            {ChainWaitOutcome::Timeout, ChainError::WaitTimeout, false},
            {ChainWaitOutcome::Pending, ChainError::WaitTimeout, false},
        };
        for (const Case &c : cases) {
            reset();
            std::array<std::uint8_t, 16> payload{};
            payload.fill(0x10U);
            PatchRegion region{0U, payload.data(), payload.size(), true, true, "t"};
            PatchPlan plan{&region, 1U};
            CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
            ChainRequest request{};
            request.carriers = carriers;
            request.carrier_count = 1U;
            request.plan = plan;

            g.wait_outcome = c.outcome;
            ChainOps ops = make_ops();
            ChainWorkspace workspace{};
            const ChainResult result = run_chain(request, ops, workspace);
            assert(result.error == c.expected);
            assert(result.lkm_loaded == c.loaded);
            assert(result.cleanup_ran);
            assert(g.release_calls == 1U);
            assert(events_end_with({Op::Trigger, Op::Wait, Op::Release}));
        }
    }

    /* ---- early returns still reach the single terminus ---- */

    void test_early_returns() {
        /* No carriers at all. */
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x01U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "r"};
        PatchPlan plan{&region, 1U};
        ChainRequest request{};
        request.plan = plan;
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        ChainResult result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::NoCarrier);
        assert(result.cleanup_ran);
        assert(workspace.carrier_attempt_count == 0U);
        assert(events_are({Op::Release}));

        /* Null carrier pointer with a non-zero count must not be dereferenced. */
        reset();
        request.carriers = nullptr;
        request.carrier_count = 1U;
        request.plan = plan;
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::NoCarrier);
        assert(result.cleanup_ran);
        assert(workspace.carrier_attempt_count == 0U);
        assert(events_are({Op::Release}));

        /* Only invalid carrier paths. */
        reset();
        CarrierTarget bad[1] = {{"/system/lib64/libc++.so", 0U}};
        request.carriers = bad;
        request.carrier_count = 1U;
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::NoCarrier);
        assert(result.cleanup_ran);
        assert(workspace.carrier_attempt_count == 1U);
        assert(workspace.carrier_attempts[0].error == ChainError::CarrierUnusable);
        assert(events_are({Op::Release}));

        /* Write surface not bound. */
        reset();
        CarrierTarget good[1] = {{"/vendor/lib64/target.so", 0U}};
        request.carriers = good;
        request.carrier_count = 1U;
        ops = ChainOps{};
        ops.read_block = fake_read_block;
        ops.trigger = fake_trigger;
        ops.wait_result = fake_wait;
        ops.release = fake_release;
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::NotAvailable);
        assert(result.cleanup_ran);
        assert(result.blocks_written == 0U);
        assert(events_are({Op::Release}));

        /* Empty / malformed plan. */
        reset();
        request.plan = PatchPlan{};
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::InvalidPlan);
        assert(result.cleanup_ran);
        assert(events_are({Op::Release}));

        reset();
        PatchRegion odd{0U, payload.data(), 17U, true, true, "odd"};
        request.plan = PatchPlan{&odd, 1U};
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::InvalidPlan);
        assert(result.blocks_written == 0U);
        assert(events_are({Op::Release}));

        /* Plan extent beyond a known carrier size. */
        reset();
        CarrierTarget sized[1] = {{"/vendor/lib64/target.so", 16U}};
        request.carriers = sized;
        PatchRegion two{0U, payload.data(), 32U, true, true, "two"};
        request.plan = PatchPlan{&two, 1U};
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::InvalidPlan);
        assert(result.blocks_written == 0U);
        assert(events_are({Op::Release}));
    }

    /* ---- the build_plan injection point runs before any write ---- */

    void test_build_plan_injection() {
        reset();
        std::array<std::uint8_t, 16> payload{};
        payload.fill(0x88U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "p"};
        g.planned_plan = PatchPlan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = PatchPlan{}; /* replaced by the injected planner */

        ChainOps ops = make_ops();
        ops.build_plan = fake_build_plan;
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::None);
        assert(result.blocks_written == 1U);
        assert(g.release_calls == 1U);
        assert(events_are({Op::BuildPlan, Op::Read, Op::Write, Op::Read,
                           Op::Trigger, Op::Wait, Op::Release}));
    }

    /* ---- rollback journal overflow is reported, not silently dropped ---- */

    void test_journal_overflow() {
        reset();
        std::array<std::uint8_t, (kChainRollbackBlocks + 1U) * 16U> payload{};
        payload.fill(0x99U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "big"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 0U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::None);
        assert(result.blocks_written == static_cast<std::uint32_t>(kChainRollbackBlocks + 1U));
        assert(result.journal_overflow);
        assert(result.cleanup_ran);
        assert(g.file[0] == 0x99U);
    }

    /* ---- target boundary assert: a region past target_size writes nothing ---- */

    void test_target_out_of_bounds() {
        reset();
        std::array<std::uint8_t, 32> payload{};
        payload.fill(0x5AU);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "oob"};
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 64U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;
        /* The region spans [0, 32); the file is only 16 bytes. */
        request.target_size = 16U;

        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        const ChainResult result = run_chain(request, ops, workspace);

        assert(result.error == ChainError::TargetOutOfBounds);
        assert(result.blocks_written == 0U);
        assert(g.write_calls == 0);
        assert(g.read_calls == 0);
        assert(g.release_calls == 1U);
        assert(events_are({Op::Release}));
    }

    /* ---- region closure: overlap and declared extent are rejected pre-write ---- */

    void test_region_closure() {
        reset();
        std::array<std::uint8_t, 16> a{};
        std::array<std::uint8_t, 16> b{};
        a.fill(0x11U);
        b.fill(0x22U);
        PatchRegion overlap[2] = {
            {0U, a.data(), a.size(), true, true, "a"},
            {8U, b.data(), b.size(), true, true, "b"},
        };
        PatchPlan plan{overlap, 2U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 64U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        ChainResult result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::InvalidPlan);
        assert(result.blocks_written == 0U);
        assert(g.write_calls == 0);
        assert(g.read_calls == 0);
        assert(events_are({Op::Release}));

        /* Declared plan extent smaller than the single region. */
        reset();
        PatchRegion big{0U, a.data(), 32U, true, true, "big"};
        PatchPlan declared{&big, 1U, 16U};
        request.plan = declared;
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::InvalidPlan);
        assert(g.write_calls == 0);
        assert(events_are({Op::Release}));
    }

    /* ---- optional pre-image assert: match proceeds, mismatch is final ---- */

    void test_preimage_assert() {
        std::array<std::uint8_t, 32> payload{};
        payload.fill(0x33U);
        std::array<std::uint8_t, 32> expected{};
        expected.fill(0x44U);
        PatchRegion region{0U, payload.data(), payload.size(), true, true, "pre"};
        region.preimage = expected.data();
        PatchPlan plan{&region, 1U};
        CarrierTarget carriers[1] = {{"/vendor/lib64/target.so", 64U}};
        ChainRequest request{};
        request.carriers = carriers;
        request.carrier_count = 1U;
        request.plan = plan;

        /* Match: the target already holds the declared original. The pre-image
         * pass reads both blocks before the journal/write/verify loop. */
        reset();
        for (std::size_t i = 0U; i < expected.size(); ++i) {
            g.file[i] = expected[i];
        }
        ChainOps ops = make_ops();
        ChainWorkspace workspace{};
        ChainResult result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::None);
        assert(result.preimage_checked);
        assert(result.blocks_written == 2U);
        assert(result.blocks_verified == 2U);
        assert(events_are({Op::Read, Op::Read,
                           Op::Read, Op::Write, Op::Read,
                           Op::Read, Op::Write, Op::Read,
                           Op::Trigger, Op::Wait, Op::Release}));

        /* Mismatch: no byte is written and the failure is final. */
        reset();
        ops = make_ops();
        workspace = ChainWorkspace{};
        result = run_chain(request, ops, workspace);
        assert(result.error == ChainError::PreImageMismatch);
        assert(!result.preimage_checked);
        assert(result.blocks_written == 0U);
        assert(g.write_calls == 0);
        assert(g.release_calls == 1U);
        assert(events_are({Op::Read, Op::Release}));
    }

} // namespace

int main() {
    test_carrier_paths();
    test_dev_carrier_escape();
    test_success_order();
    test_hook_stage_order();
    test_carrier_fallback();
    test_all_carriers_unusable();
    test_write_failure_rollback();
    test_verify_mismatch_rollback();
    test_trigger_failure();
    test_wait_outcomes();
    test_early_returns();
    test_build_plan_injection();
    test_journal_overflow();
    test_target_out_of_bounds();
    test_region_closure();
    test_preimage_assert();

    std::puts("cve_2026_43284_chain_test: OK");
    return 0;
}
