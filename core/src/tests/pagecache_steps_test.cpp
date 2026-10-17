/** @file tests/pagecache_steps_test.cpp
 *  封装步骤的证据：名字映射，以及「按块写入 + 每块回读」这一共用实现。
 *
 *  判据五条：① 五个名字往返一致，别的名字不认；② 一个 40 字节镜像按 16 字节宽写成 3 块，
 *  偏移逐块递进、最后一块补零；③ 写被拒 ⇒ step-write-refused，回读不符 ⇒ step-verify-failed；
 *  ④ 空镜像 / 空 access 各有一个具名拒绝；⑤ 回读抽样：规则一处、次数 ≈ blocks/32+2、
 *  抽样块不符整段失败并把该块记进 failed_block、verify_every=0 时每块都读；
 *  ⑥ journal / rollback：第 k 块失败 ⇒ 旧块**逆序**写回且每块回读一致、容量不足 ⇒ overflow 且
 *  **继续写**、回滚里某块写不回 ⇒ incomplete 且其余仍回滚、成功路径 rolled_back=0。
 *  假 access 记录每一次读写，不碰设备。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"
#include "backend/cve_2026_43284/steps/pagecache_step_runner.hpp"

using namespace ghostlock;

namespace {
    /* 命名空间别名：不是 using 声明，按 2026-10-10 的规则这样写。 */
    namespace steps = backend::cve_2026_43284::steps;
    namespace cve = backend::cve_2026_43284;
    /* contract:: 已在 ghostlock 下可见（文件级 using namespace ghostlock）。 */

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    /** A recording fake: every write lands where the offset says, and reads come back from there. */
    class FakeAccess final : public contract::KernelAccess {
      public:
        std::vector<std::uint64_t> offsets{};
        std::vector<std::size_t> widths{};
        /** Reads are recorded too: a test can then see *which* read came back last -- the read-back
         *  of the site block has to be the one that follows the site write, not the pre-check. */
        std::vector<std::uint64_t> read_offsets{};
        bool refuse_write = false;
        bool corrupt = false;
        /** The offset whose read-back comes back altered (one byte); kNoCorruption means none.
         *  This is how a test makes one specific block fail its verify while every other lands. */
        static constexpr std::uint64_t kNoCorruption = ~std::uint64_t{0U};
        std::uint64_t corrupt_at = kNoCorruption;
        /** 回滚证据用的闸门：某个偏移上第 N 次（0 起）写被拒，其余写照常。
         *  kNoCorruption = 不设。让原写成功、回滚那一次被拒 ⇒ 只有回滚断，写阶段不受影响。 */
        std::uint64_t refuse_write_at = kNoCorruption;
        std::size_t refuse_write_attempt = 0U;
        std::size_t writes_at_refused_slot = 0U;
        /** Big enough for the offsets this test uses (0x1000+): the guard is the unit's business,
         *  not something the test should trip by accident. */
        static constexpr std::size_t kMemoryBytes = 1U << 20U;
        std::byte *memory = nullptr;
        FakeAccess() noexcept : memory(new std::byte[kMemoryBytes]{}) {}

        [[nodiscard]] AccessState state(contract::AccessChannel) const noexcept override { return AccessState::Ready; }
        [[nodiscard]] bool supports(contract::AccessChannel) const noexcept override { return true; }
        [[nodiscard]] bool supports(contract::AccessCarrier) const noexcept override { return true; }
        [[nodiscard]] bool supports_width(std::size_t) const noexcept override { return true; }
        [[nodiscard]] AccessResult<bool> initialize(const contract::KernelAccess::AccessMiscParameters &) noexcept override {
            return AccessResult<bool>::ok(true);
        }
        [[nodiscard]] AccessResult<bool> shutdown() noexcept override { return AccessResult<bool>::ok(true); }
        [[nodiscard]] AccessResult<std::span<const std::byte>> read(
                std::uint64_t, std::size_t size, contract::AccessChannel, const contract::KernelAccess::AccessMiscParameters &parameters) noexcept override {
            const auto at = static_cast<std::size_t>(offset_of(parameters));
            read_offsets.push_back(offset_of(parameters));
            if (at + size > kMemoryBytes) { return AccessResult<std::span<const std::byte>>::failed(Status::Unsupported); }
            for (std::size_t i = 0U; i < size; ++i) { scratch[i] = memory[at + i]; }
            if (corrupt || offset_of(parameters) == corrupt_at) { scratch[0] = static_cast<std::byte>(0x7FU); }
            return AccessResult<std::span<const std::byte>>::ok(
                    std::span<const std::byte>(&scratch[0], size));
        }
        [[nodiscard]] AccessResult<std::size_t> write(
                std::uint64_t, std::span<const std::byte> in, contract::AccessChannel,
                const contract::KernelAccess::AccessMiscParameters &parameters) noexcept override {
            offsets.push_back(offset_of(parameters));
            widths.push_back(in.size());
            if (refuse_write) { return AccessResult<std::size_t>::failed(Status::Unsupported); }
            if (offset_of(parameters) == refuse_write_at) {
                const std::size_t attempt = writes_at_refused_slot;
                ++writes_at_refused_slot;
                if (attempt == refuse_write_attempt) {
                    return AccessResult<std::size_t>::failed(Status::Unsupported);
                }
            }
            const auto at = static_cast<std::size_t>(offset_of(parameters));
            if (at + in.size() > kMemoryBytes) { return AccessResult<std::size_t>::failed(Status::Unsupported); }
            for (std::size_t i = 0U; i < in.size(); ++i) { memory[at + i] = in[i]; }
            return AccessResult<std::size_t>::ok(in.size());
        }
        /* The word-level members have defaults built on read()/write(); this fake can do better,
         *  so it overrides them -- with the signatures the interface declares, or not at all. */
        [[nodiscard]] AccessResult<std::uint64_t> read64(
                std::uint64_t, contract::AccessChannel, const contract::KernelAccess::AccessMiscParameters &) noexcept override {
            return AccessResult<std::uint64_t>::failed(Status::Unsupported);
        }
        [[nodiscard]] AccessResult<std::size_t> write64(
                std::uint64_t, std::uint64_t, contract::AccessChannel, const contract::KernelAccess::AccessMiscParameters &) noexcept override {
            return AccessResult<std::size_t>::failed(Status::Unsupported);
        }
        [[nodiscard]] AccessResult<std::size_t> write_zero(
                std::uint64_t, contract::AccessChannel, const contract::KernelAccess::AccessMiscParameters &) noexcept override {
            return AccessResult<std::size_t>::failed(Status::Unsupported);
        }

      public:
        ~FakeAccess() noexcept override { delete[] memory; }

      private:
        /** The offset the caller set lives in the backend"s own parameter type. */
        [[nodiscard]] static std::uint64_t offset_of(const contract::KernelAccess::AccessMiscParameters &parameters) noexcept {
            return static_cast<const cve::AccessParams &>(parameters).offset;
        }
        std::byte scratch[256] = {};
    };

    /** The fake world the injected existence check reads. Static because the interface hands it a
     *  bare function pointer; the test owns it and every case sets it up before the call. */
    struct FakeWorld final {
        const char *present[8] = {};
        std::size_t present_count = 0U;
        std::uint32_t slept_ms = 0U;
        /** -1 表示一直按 present 表回答；>=0 表示只看前 N 次调用。 */
        long long present_calls = -1;
        long long calls = 0;
    };

    FakeWorld *g_world = nullptr;

    bool world_exists(const char *path) noexcept {
        if (g_world == nullptr) { return false; }
        ++g_world->calls;
        if (g_world->present_calls >= 0 && g_world->calls > g_world->present_calls) { return false; }
        for (std::size_t i = 0U; i < g_world->present_count; ++i) {
            if (std::strcmp(g_world->present[i], path) == 0) { return true; }
        }
        return false;
    }

    void world_sleep(std::uint32_t ms) noexcept {
        if (g_world != nullptr) { g_world->slept_ms += ms; }
    }
    void names_map_both_ways() {
        const char *expected[] = {"patch1", "carrier", "hook", "trigger", "wait"};
        expect(steps::kPagecacheStepCount == 5U, "five steps are declared");
        for (std::size_t i = 0U; i < steps::kPagecacheStepCount; ++i) {
            steps::PagecacheStep step{};
            const bool known = steps::pagecache_step_by_name(expected[i], step);
            expect(known && std::strcmp(steps::pagecache_step_name(step), expected[i]) == 0,
                   "a name round-trips");
        }
        steps::PagecacheStep unused{};
        expect(!steps::pagecache_step_by_name("w9", unused), "an unknown name is not a step");
    }

    void an_image_is_written_block_by_block() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[40] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = static_cast<std::byte>(i + 1U); }
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x1000U;
        request.width = 16U;
        const char *refusal = nullptr;
        const bool wrote = steps::write_image_verified(request, &refusal);
        if (!wrote) { std::printf("  (refusal: %s, blocks recorded: %zu)\n", refusal == nullptr ? "-" : refusal, access.offsets.size()); }
        expect(wrote, "a 40-byte image goes in");
        expect(access.offsets.size() == 3U, "it takes three blocks");
        expect(access.offsets.size() == 3U && access.offsets[0] == 0x1000U && access.offsets[1] == 0x1010U &&
                       access.offsets[2] == 0x1020U,
               "each block sits at its own offset");
        expect(access.widths.size() == 3U && access.widths[2] == 16U, "every block is one width");
        expect(access.memory[0x1028] == std::byte{0}, "the short last block is zero-padded");
    }

    /** (f) 回读抽样的规则只有一处（verify_this_block）：四条子句各有一块证据。 */
    void the_sampling_rule_has_four_clauses() {
        expect(steps::verify_this_block(0U, 1U, 32U, false), "a one-block segment reads its only block");
        expect(steps::verify_this_block(0U, 100U, 32U, false), "the first block is always read");
        expect(steps::verify_this_block(99U, 100U, 32U, false), "the last block is always read");
        expect(steps::verify_this_block(32U, 100U, 32U, false) &&
                       steps::verify_this_block(64U, 100U, 32U, false),
               "every Nth block is read");
        expect(!steps::verify_this_block(31U, 100U, 32U, false), "a block between samples is not");
        expect(steps::verify_this_block(31U, 100U, 32U, true), "but the block after a failure is");
        expect(steps::verify_this_block(31U, 100U, 0U, false), "verify_every=0 reads every block");
        expect(!steps::verify_this_block(100U, 100U, 32U, false), "an index past the end is not a block");
    }

    /** (g) verify_every=32 的整段（769 块 = 载体实测块数）：回读次数 ≈ blocks/32+2，且只落在采样点。 */
    void thirty_two_samples_one_block_in_thirty_two() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 769U;
        constexpr std::uint64_t kBase = 0x1000U;
        FakeAccess access{};
        cve::AccessParams params{};
        std::vector<std::byte> image(kBlocks * kWidth, std::byte{0x5AU});
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image.data(), image.size());
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 32U;
        const char *refusal = nullptr;
        const bool wrote = steps::write_image_verified(request, &refusal);
        if (!wrote) { std::printf("  (refusal: %s)\n", refusal == nullptr ? "-" : refusal); }
        expect(wrote, "the whole carrier goes in");
        expect(access.offsets.size() == kBlocks, "every block is written");
        const std::size_t reads = access.read_offsets.size();
        expect(reads <= (kBlocks / 32U) + 2U, "the readbacks are no more than blocks/32+2");
        expect(reads >= kBlocks / 32U, "and not fewer than blocks/32");
        bool only_samples = true;
        for (const std::uint64_t at : access.read_offsets) {
            const auto block = static_cast<std::size_t>((at - kBase) / kWidth);
            if (!steps::verify_this_block(block, kBlocks, 32U, false)) { only_samples = false; }
        }
        expect(only_samples, "every read is a sample point");
        expect(request.failed_block == steps::kNoFailedBlock, "nothing failed, so nothing is recorded");
    }

    /** (h) verify_every=0 ⇒ 每块都回读（要全程证据时的行为）。 */
    void zero_reads_every_block_back() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 8U;
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x3000U;
        request.width = kWidth;
        request.verify_every = 0U;
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal), "the segment goes in");
        expect(access.read_offsets.size() == kBlocks, "every block is read back");
        bool in_order = access.read_offsets.size() == kBlocks;
        for (std::size_t block = 0U; block < kBlocks && in_order; ++block) {
            if (access.read_offsets[block] != 0x3000U + block * kWidth) { in_order = false; }
        }
        expect(in_order, "at each block's own offset, in order");
    }

    /** (i) 抽样块回读不符 ⇒ 整段失败、失败块被记下、且它之后没有块被写（fail-closed）。
     *  这一块确实是**被读过**的（失败行随之逐块打印，不受 512 字节门限）。 */
    void a_sampled_block_that_does_not_come_back_fails_the_segment() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 64U;
        constexpr std::uint64_t kBase = 0x2000U;
        constexpr std::size_t kBadBlock = 32U;
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 32U;
        request.budget_ms = 0U;   /* 不重试：一次不符就是失败 */
        access.corrupt_at = kBase + kBadBlock * kWidth;
        const char *refusal = nullptr;
        expect(!steps::write_image_verified(request, &refusal), "one bad sampled block fails the segment");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed") == 0, "by name");
        expect(request.failed_block == kBadBlock, "and the block that did not come back is recorded");
        expect(access.offsets.size() == kBadBlock + 1U, "nothing after it is written");
        std::size_t reads_of_bad = 0U;
        for (const std::uint64_t at : access.read_offsets) {
            if (at == kBase + kBadBlock * kWidth) { ++reads_of_bad; }
        }
        expect(reads_of_bad == 1U, "the failing block was read back once");
        expect(access.read_offsets.size() >= 2U, "and the per-block scan followed it");
    }

    /** (j) 失败行不随 512 字节门限走：抽样间隔 3、坏块 33（at = 528 B，**不在** 512 边界上）
     *  ⇒ 仍然整段失败、仍然记下这一块 —— 失败路径是逐块的，抽样只决定读不读。 */
    void a_failure_off_the_log_cadence_is_still_recorded() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 64U;
        constexpr std::uint64_t kBase = 0x4000U;
        constexpr std::size_t kBadBlock = 33U;
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 3U;
        request.budget_ms = 0U;
        access.corrupt_at = kBase + kBadBlock * kWidth;
        const char *refusal = nullptr;
        expect(!steps::write_image_verified(request, &refusal), "a bad block between log lines still fails");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed") == 0, "by name");
        expect(request.failed_block == kBadBlock, "and it is the recorded block");
        expect((kBadBlock * kWidth) % 512U != 0U, "the block is not on the 512-byte log boundary");
        std::size_t reads_of_bad = 0U;
        for (const std::uint64_t at : access.read_offsets) {
            if (at == kBase + kBadBlock * kWidth) { ++reads_of_bad; }
        }
        expect(reads_of_bad == 1U, "it was read back (so its failure line is the per-block one)");
        expect(access.offsets.size() == kBadBlock + 1U, "nothing after it is written");
    }

    /** 把段内的字节铺成 old：写之前的页内容，回滚之后必须逐字节回到这里。 */
    void fill_block(FakeAccess &access, std::uint64_t at, std::size_t bytes, std::byte old) noexcept {
        for (std::size_t i = 0U; i < bytes; ++i) { access.memory[at + i] = old; }
    }

    /** 某一块是否逐字节等于 want。 */
    [[nodiscard]] bool block_is(const FakeAccess &access, std::uint64_t at, std::size_t width,
                                std::byte want) noexcept {
        for (std::size_t i = 0U; i < width; ++i) {
            if (access.memory[at + i] != want) { return false; }
        }
        return true;
    }

    /** (k) 第 k 块回读不符 ⇒ journal 里的旧块**逆序**写回，且每块写后回读一致（判据 a）。
     *  失败的这一块在日志里是逐行的那一条（不受 512 字节门限），回滚行也逐块保留。 */
    void a_verified_failure_rolls_the_journal_back_in_reverse() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 8U;
        constexpr std::size_t kBadBlock = 5U;
        constexpr std::uint64_t kBase = 0x6000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        cve::AccessParams params{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        std::byte journal[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;   /* 每块都回读：失败点确定在第 k 块 */
        request.journal = std::span<std::byte>(journal, sizeof(journal));
        access.corrupt_at = kBase + static_cast<std::uint64_t>(kBadBlock) * kWidth;
        steps::WriteOutcome outcome{};
        const char *refusal = nullptr;
        expect(!steps::write_image_verified(request, &refusal, &outcome), "a bad block fails the segment");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed") == 0, "by name");
        expect(request.failed_block == kBadBlock, "and the block is recorded");
        expect(outcome.written == kBadBlock + 1U, "written counts the block that did not come back");
        expect(outcome.verified == kBadBlock, "verified counts the ones that did");
        expect(outcome.blocks_rolled_back == kBadBlock + 1U, "every journaled block was written back");
        expect(!outcome.rollback_incomplete, "and every write-back came back");
        expect(!outcome.journal_overflow, "the journal was big enough");
        expect(access.offsets.size() == 2U * (kBadBlock + 1U), "one restore per journaled block");
        bool newest_first = access.offsets.size() == 2U * (kBadBlock + 1U);
        for (std::size_t i = 0U; i <= kBadBlock && newest_first; ++i) {
            const std::uint64_t want = kBase + static_cast<std::uint64_t>(kBadBlock - i) * kWidth;
            if (access.offsets[kBadBlock + 1U + i] != want) { newest_first = false; }
        }
        expect(newest_first, "the restores run newest first");
        bool prefix_back = true;
        for (std::size_t block = 0U; block < kBadBlock; ++block) {
            if (!block_is(access, kBase + static_cast<std::uint64_t>(block) * kWidth, kWidth, kOld)) {
                prefix_back = false;
            }
        }
        expect(prefix_back, "the journaled prefix is back to what it held");
        bool bad_as_journaled = access.memory[kBase + kBadBlock * kWidth] == std::byte{0x7F};
        for (std::size_t i = 1U; i < kWidth && bad_as_journaled; ++i) {
            if (access.memory[kBase + kBadBlock * kWidth + i] != kOld) { bad_as_journaled = false; }
        }
        expect(bad_as_journaled, "the bad block came back as the page read then");
        expect(journal[0] == kOld && journal[kBadBlock * kWidth] == std::byte{0x7F},
               "one block-sized record per journaled block");
    }

    /** (l) journal 容量不足 ⇒ overflow=1 且**仍然继续写**（判据 b）：溢出只记录，不中止。 */
    void a_short_journal_overflows_and_keeps_writing() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 4U;
        constexpr std::size_t kCapacityBlocks = 2U;
        constexpr std::uint64_t kBase = 0x7000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        cve::AccessParams params{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        std::byte journal[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        request.journal = std::span<std::byte>(journal, sizeof(journal));
        /* 存储够 4 条，但**声明**容量只有 2 条：声明的那一项说了算。 */
        request.journal_capacity_bytes = kCapacityBlocks * kWidth;
        steps::WriteOutcome outcome{};
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal, &outcome), "a full journal does not stop the write");
        expect(outcome.journal_overflow, "the overflow is recorded");
        expect(outcome.written == kBlocks && outcome.verified == kBlocks, "every block went in and came back");
        expect(outcome.blocks_rolled_back == 0U && !outcome.rollback_incomplete, "nothing to roll back");
        expect(access.offsets.size() == kBlocks, "the blocks after the overflow are still written");
        bool landed = true;
        for (std::size_t block = 0U; block < kBlocks; ++block) {
            if (!block_is(access, kBase + static_cast<std::uint64_t>(block) * kWidth, kWidth, kNew)) {
                landed = false;
            }
        }
        expect(landed, "the page holds the image");
        std::size_t reads_of[kBlocks] = {};
        for (const std::uint64_t at : access.read_offsets) {
            const auto block = static_cast<std::size_t>((at - kBase) / kWidth);
            if (block < kBlocks) { ++reads_of[block]; }
        }
        expect(reads_of[0] == 2U && reads_of[1] == 2U, "the journaled blocks are read before and after");
        expect(reads_of[2] == 1U && reads_of[3] == 1U, "past the capacity only the read-back remains");
        expect(journal[0] == kOld && journal[kCapacityBlocks * kWidth] == std::byte{0},
               "nothing is recorded past the declared capacity");
    }

    /** (m) 回滚里某块写不回 ⇒ incomplete=1，其余块仍回滚（判据 c）。 */
    void a_block_that_cannot_be_written_back_is_incomplete() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 6U;
        constexpr std::size_t kBadBlock = 4U;    /* 回读不符，触发回滚 */
        constexpr std::size_t kStuckBlock = 2U;  /* 回滚时写被拒 */
        constexpr std::uint64_t kBase = 0x8000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        cve::AccessParams params{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        std::byte journal[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        request.journal = std::span<std::byte>(journal, sizeof(journal));
        access.corrupt_at = kBase + static_cast<std::uint64_t>(kBadBlock) * kWidth;
        access.refuse_write_at = kBase + static_cast<std::uint64_t>(kStuckBlock) * kWidth;
        access.refuse_write_attempt = 1U;  /* 0 = 原写（成功），1 = 回滚那一次（被拒） */
        steps::WriteOutcome outcome{};
        const char *refusal = nullptr;
        expect(!steps::write_image_verified(request, &refusal, &outcome), "the segment fails");
        expect(outcome.rollback_incomplete, "the block that could not be written back is recorded");
        expect(outcome.blocks_rolled_back == kBadBlock, "the other journaled blocks are still written back");
        expect(access.memory[kBase + kStuckBlock * kWidth] == kNew, "the stuck block keeps the image");
        bool others_back = true;
        for (std::size_t block = 0U; block < kBadBlock; ++block) {
            if (block == kStuckBlock) { continue; }
            if (!block_is(access, kBase + static_cast<std::uint64_t>(block) * kWidth, kWidth, kOld)) {
                others_back = false;
            }
        }
        expect(others_back, "the rest is back to what it held");
        expect(access.offsets.size() == 2U * (kBadBlock + 1U),
               "every journaled block was attempted, not just the ones before the stuck one");
        bool newest_first = access.offsets.size() == 2U * (kBadBlock + 1U);
        for (std::size_t i = 0U; i <= kBadBlock && newest_first; ++i) {
            const std::uint64_t want = kBase + static_cast<std::uint64_t>(kBadBlock - i) * kWidth;
            if (access.offsets[kBadBlock + 1U + i] != want) { newest_first = false; }
        }
        expect(newest_first, "and the attempts still run newest first");
    }

    /** (n) 成功路径：一条记录都不写回，rolled_back 保持 0（判据 d）。 */
    void a_clean_segment_rolls_back_nothing() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 8U;
        constexpr std::uint64_t kBase = 0x9000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        cve::AccessParams params{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        std::byte journal[kBlocks * kWidth] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        request.journal = std::span<std::byte>(journal, sizeof(journal));
        steps::WriteOutcome outcome{};
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal, &outcome), "the segment goes in");
        expect(outcome.written == kBlocks && outcome.verified == kBlocks, "every block was written and verified");
        expect(outcome.blocks_rolled_back == 0U, "nothing is written back on success");
        expect(!outcome.journal_overflow && !outcome.rollback_incomplete, "neither flag is raised");
        expect(access.offsets.size() == kBlocks, "the write surface saw only the image");
        expect(access.read_offsets.size() == 2U * kBlocks, "each block is read once to journal and once to verify");
        expect(journal[0] == kOld && journal[(kBlocks - 1U) * kWidth] == kOld,
               "every old block is recorded, first and last included");
    }

    /* ---------------------------------------------------------------------
     * 执行体级：journal 缓冲由调用方给（pagecache_step_runner.hpp）。三处必须一致：
     * 缓冲里记下的字节、写回发生的次序、JournaledWrite 的字段。
     * ------------------------------------------------------------------- */

    /** (o) 第 k 块失败 ⇒ 调用方缓冲被填、回滚按逆序发生、outcome 字段正确（判据 a）。 */
    void the_runner_journals_and_rolls_back_in_reverse() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 8U;
        constexpr std::size_t kBadBlock = 5U;
        constexpr std::uint64_t kBase = 0xA000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        const std::span<const std::byte> whole(image, sizeof(image));
        std::vector<std::byte> journal(steps::journal_bytes_for(whole, kWidth));
        steps::WriteRequest request{};
        cve::AccessParams params{};
        request.access = &access;
        request.params = &params;
        request.image = whole;
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        access.corrupt_at = kBase + static_cast<std::uint64_t>(kBadBlock) * kWidth;
        char decorated[128] = {};
        const char *refusal = nullptr;
        const steps::JournaledWrite ledger = steps::write_image_journaled(
                request, std::span<std::byte>(journal.data(), journal.size()), &refusal,
                std::span<char>(decorated, sizeof(decorated)));
        if (ledger.ok) { std::printf("  (runner: the corrupted block did not fail the segment)\n"); }
        expect(!ledger.ok, "a corrupted block fails the segment");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed") == 0,
               "and the refusal keeps its name");
        expect(journal.size() == kBlocks * kWidth, "the caller sized the buffer by blocks, not by bytes");
        expect(ledger.blocks == kBlocks && ledger.capacity_blocks == kBlocks,
               "one record per block, capacity counted in blocks");
        expect(ledger.entries == kBadBlock + 1U, "every attempted block left a record");
        expect(ledger.outcome.written == kBadBlock + 1U && ledger.outcome.verified == kBadBlock,
               "written counts the block that did not come back, verified the ones that did");
        expect(ledger.outcome.blocks_rolled_back == kBadBlock + 1U, "every record was written back");
        expect(!ledger.outcome.rollback_incomplete && !ledger.outcome.journal_overflow,
               "the buffer was big enough and every write-back came back");
        expect(static_cast<unsigned>(std::to_integer<std::uint8_t>(journal[0])) == 0xEEU,
               "the first record holds what that block held");
        expect(static_cast<unsigned>(std::to_integer<std::uint8_t>(journal[kBadBlock * kWidth])) == 0x7FU,
               "the last record holds what the failing read returned");
        bool newest_first = access.offsets.size() == 2U * (kBadBlock + 1U);
        for (std::size_t i = 0U; i <= kBadBlock && newest_first; ++i) {
            const std::uint64_t want = kBase + static_cast<std::uint64_t>(kBadBlock - i) * kWidth;
            if (access.offsets[kBadBlock + 1U + i] != want) { newest_first = false; }
        }
        expect(newest_first, "the rollback runs newest first");
    }

    /** (p) 容量刚好不足一块 ⇒ overflow=1 且仍继续写（判据 b）：溢出只记录，不中止。 */
    void a_short_runner_journal_overflows_and_keeps_writing() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 4U;
        constexpr std::size_t kCapacityBlocks = 3U;   /* 刚好少一条 */
        constexpr std::uint64_t kBase = 0xB000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        const std::span<const std::byte> whole(image, sizeof(image));
        std::vector<std::byte> journal(kCapacityBlocks * kWidth);
        steps::WriteRequest request{};
        cve::AccessParams params{};
        request.access = &access;
        request.params = &params;
        request.image = whole;
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        char decorated[128] = {};
        const char *refusal = nullptr;
        const steps::JournaledWrite ledger = steps::write_image_journaled(
                request, std::span<std::byte>(journal.data(), journal.size()), &refusal,
                std::span<char>(decorated, sizeof(decorated)));
        expect(ledger.ok, "a short buffer does not stop the write");
        expect(ledger.capacity_blocks == kCapacityBlocks, "capacity is counted in blocks");
        expect(ledger.entries == kCapacityBlocks, "the buffer holds exactly what fits");
        expect(ledger.outcome.journal_overflow, "the overflow is reported");
        expect(ledger.outcome.written == kBlocks && ledger.outcome.verified == kBlocks,
               "every block still went in and came back");
        expect(access.offsets.size() == kBlocks, "the blocks after the overflow are still written");
        bool landed = true;
        for (std::size_t block = 0U; block < kBlocks; ++block) {
            if (!block_is(access, kBase + static_cast<std::uint64_t>(block) * kWidth, kWidth, kNew)) {
                landed = false;
            }
        }
        expect(landed, "the page holds the whole image");
        expect(ledger.outcome.blocks_rolled_back == 0U && !ledger.outcome.rollback_incomplete,
               "nothing was rolled back");
    }

    /** (q) 无失败 ⇒ rolled=0、overflow=0，记录照记（判据 c）。 */
    void a_clean_runner_segment_rolls_back_nothing() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 6U;
        constexpr std::uint64_t kBase = 0xC000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        const std::span<const std::byte> whole(image, sizeof(image));
        std::vector<std::byte> journal(steps::journal_bytes_for(whole, kWidth));
        steps::WriteRequest request{};
        cve::AccessParams params{};
        request.access = &access;
        request.params = &params;
        request.image = whole;
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        char decorated[128] = {};
        const char *refusal = nullptr;
        const steps::JournaledWrite ledger = steps::write_image_journaled(
                request, std::span<std::byte>(journal.data(), journal.size()), &refusal,
                std::span<char>(decorated, sizeof(decorated)));
        expect(ledger.ok, "the segment goes in");
        expect(ledger.entries == kBlocks && ledger.capacity_blocks == kBlocks, "every block is recorded");
        expect(ledger.outcome.written == kBlocks && ledger.outcome.verified == kBlocks,
               "every block was written and verified");
        expect(ledger.outcome.blocks_rolled_back == 0U && !ledger.outcome.journal_overflow &&
                       !ledger.outcome.rollback_incomplete,
               "a clean segment raises neither flag and writes nothing back");
        expect(access.offsets.size() == kBlocks, "the surface saw only the image");
        expect(journal[0] == kOld && journal[(kBlocks - 1U) * kWidth] == kOld,
               "every old block is in the buffer, first and last included");
    }

    /** (r) 回滚里有块写不回 ⇒ incomplete=1，且拒绝串末尾带具名后缀（执行体的契约）。 */
    void an_incomplete_rollback_names_itself() {
        constexpr std::size_t kWidth = 16U;
        constexpr std::size_t kBlocks = 4U;
        constexpr std::size_t kBadBlock = 3U;
        constexpr std::uint64_t kBase = 0xD000U;
        const std::byte kOld{0xEE};
        const std::byte kNew{0x5A};
        FakeAccess access{};
        fill_block(access, kBase, kBlocks * kWidth, kOld);
        std::byte image[kBlocks * kWidth] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = kNew; }
        const std::span<const std::byte> whole(image, sizeof(image));
        std::vector<std::byte> journal(steps::journal_bytes_for(whole, kWidth));
        steps::WriteRequest request{};
        cve::AccessParams params{};
        request.access = &access;
        request.params = &params;
        request.image = whole;
        request.offset = kBase;
        request.width = kWidth;
        request.verify_every = 0U;
        access.corrupt_at = kBase + static_cast<std::uint64_t>(kBadBlock) * kWidth;
        /* 第 2 次写 kBase 就是回滚那一次：让它被拒 ⇒ 只有这一块救不回来。 */
        access.refuse_write_at = kBase;
        access.refuse_write_attempt = 1U;
        char decorated[128] = {};
        const char *refusal = nullptr;
        const steps::JournaledWrite ledger = steps::write_image_journaled(
                request, std::span<std::byte>(journal.data(), journal.size()), &refusal,
                std::span<char>(decorated, sizeof(decorated)));
        expect(!ledger.ok, "the segment still fails");
        expect(ledger.outcome.rollback_incomplete, "a block that cannot be written back is counted");
        expect(ledger.outcome.blocks_rolled_back == kBlocks - 1U, "the other records are still written back");
        expect(ledger.entries == kBlocks, "every attempted block left a record");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed:rollback-incomplete") == 0,
               "the named reason carries the suffix");
    }

    void the_hook_lands_and_verifies() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const std::uint64_t block_at = plan.hook_file_offset - (plan.hook_file_offset % kWidth);
        const std::size_t within = static_cast<std::size_t>(plan.hook_file_offset - block_at);
        expect(within >= 4U && block_at + kWidth <= FakeAccess::kMemoryBytes, "the plan fits the fake");
        FakeAccess access{};
        cve::AccessParams params{};
        /* The words the plan demands: the guard before the site and the instruction it displaces. */
        const auto put_le32 = [&](std::size_t at, std::uint32_t word) noexcept {
            access.memory[at] = static_cast<std::byte>(word & 0xFFU);
            access.memory[at + 1U] = static_cast<std::byte>((word >> 8U) & 0xFFU);
            access.memory[at + 2U] = static_cast<std::byte>((word >> 16U) & 0xFFU);
            access.memory[at + 3U] = static_cast<std::byte>((word >> 24U) & 0xFFU);
        };
        put_le32(static_cast<std::size_t>(block_at) + within - 4U, plan.guard_instruction);
        put_le32(static_cast<std::size_t>(block_at) + within, plan.displaced_instruction);
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWidth;
        const char *refusal = nullptr;
        const bool hooked = steps::write_hook(request, &refusal);
        if (!hooked) { std::printf("  (hook refusal: %s)\n", refusal == nullptr ? "-" : refusal); }
        expect(hooked, "the hook lands");
        /* The site word is no longer the one it displaced: the branch replaced it. */
        const auto word = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                access.memory[static_cast<std::size_t>(block_at) + within]));
        expect(word != static_cast<std::uint32_t>(plan.displaced_instruction & 0xFFU),
               "the site word was patched");
        /* And the payload is where the plan says, byte for byte. */
        std::uint8_t expected[64U * 64U] = {};
        std::size_t expected_size = 0U;
        expect(cve::hook::build_sentry_payload(plan, expected, sizeof(expected), expected_size) == cve::hook::SentryError::Ok,
               "the payload builds");
        bool same = expected_size != 0U;
        for (std::size_t i = 0U; i < expected_size; ++i) {
            if (access.memory[static_cast<std::size_t>(plan.payload_file_offset) + i] !=
                static_cast<std::byte>(expected[i])) {
                same = false;
            }
        }
        expect(same, "the payload is in place");
        /* A plan whose guard is not there is refused by name, not written over. */
        FakeAccess bare{};
        steps::HookRequest wrong{};
        wrong.access = &bare;
        wrong.params = &params;
        wrong.plan = &plan;
        wrong.width = kWidth;
        expect(!steps::write_hook(wrong, &refusal) && std::strcmp(refusal, "step-guard-mismatch") == 0,
               "a missing guard names itself");
    }
    /** The geometry a hook test needs: which block encloses the site, where inside it the site sits,
     *  and how many blocks the payload takes. Derived from the plan itself, never assumed. */
    struct HookGeometry final {
        std::uint64_t site_block = 0U;
        std::size_t within = 0U;
        std::size_t payload_blocks = 0U;
        std::size_t payload_bytes = 0U;
    };

    HookGeometry geometry_of(const cve::hook::SentryPlan &plan, std::size_t width) noexcept {
        HookGeometry geometry{};
        geometry.site_block = plan.hook_file_offset - (plan.hook_file_offset % width);
        geometry.within = static_cast<std::size_t>(plan.hook_file_offset - geometry.site_block);
        std::uint8_t payload[64U * 64U] = {};
        std::size_t size = 0U;
        if (cve::hook::build_sentry_payload(plan, payload, sizeof(payload), size) == cve::hook::SentryError::Ok) {
            geometry.payload_bytes = size;
            geometry.payload_blocks = (size + width - 1U) / width;
        }
        return geometry;
    }

    void put_le32(std::byte *memory, std::size_t at, std::uint32_t word) noexcept {
        memory[at] = static_cast<std::byte>(word & 0xFFU);
        memory[at + 1U] = static_cast<std::byte>((word >> 8U) & 0xFFU);
        memory[at + 2U] = static_cast<std::byte>((word >> 16U) & 0xFFU);
        memory[at + 3U] = static_cast<std::byte>((word >> 24U) & 0xFFU);
    }

    std::uint32_t get_le32(const std::byte *memory, std::size_t at) noexcept {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(memory[at])) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(memory[at + 1U])) << 8U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(memory[at + 2U])) << 16U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(memory[at + 3U])) << 24U);
    }

    /** The image the plan expects: guard word before the site, displaced word at it. */
    void plant_site(FakeAccess &access, const cve::hook::SentryPlan &plan, std::size_t width) noexcept {
        const auto geometry = geometry_of(plan, width);
        const auto site = static_cast<std::size_t>(geometry.site_block) + geometry.within;
        put_le32(access.memory, site - 4U, plan.guard_instruction);
        put_le32(access.memory, site, plan.displaced_instruction);
    }

    /** (a) 写序：先载荷的每一块（从 payload_file_offset 起、逐块），**最后**才是包裹站点的块。
     *  参考：成功历史 hook_patch.cpp:176-189 先 shellcode 全部块、再写 trampoline 块；上游
     *  exp.c:716-740 同序。站点先写会让分支指向尚未写入的 landing，走到该 sentry 的进程执行零字节。 */
    void the_payload_lands_before_the_site() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const auto geometry = geometry_of(plan, kWidth);
        expect(geometry.payload_blocks > 1U, "the payload spans several blocks");
        expect(geometry.site_block + kWidth <= FakeAccess::kMemoryBytes, "the site block fits the fake");
        FakeAccess access{};
        cve::AccessParams params{};
        plant_site(access, plan, kWidth);
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWidth;
        const char *refusal = nullptr;
        const bool hooked = steps::write_hook(request, &refusal);
        if (!hooked) { std::printf("  (hook refusal: %s)\n", refusal == nullptr ? "-" : refusal); }
        expect(hooked, "the hook lands");
        expect(access.offsets.size() == geometry.payload_blocks + 1U,
               "the hook writes every payload block and the site block, once each");
        bool payload_in_order = access.offsets.size() == geometry.payload_blocks + 1U;
        for (std::size_t block = 0U; block < geometry.payload_blocks && payload_in_order; ++block) {
            if (access.offsets[block] != plan.payload_file_offset + block * kWidth) { payload_in_order = false; }
        }
        expect(payload_in_order, "the payload goes in from payload_file_offset, block by block");
        expect(!access.offsets.empty() && access.offsets.back() == geometry.site_block,
               "the block that carries the site is written last");
    }

    /** (b) 任一载荷块失败 ⇒ 站点块从未被写过：分支不得在 landing 就绪前生效。
     *  逐块让回读失真，每一轮都是一次全新的写钩子，唯一的坏块就是那一块。 */
    void a_failed_payload_block_never_writes_the_site() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const auto geometry = geometry_of(plan, kWidth);
        const auto site = static_cast<std::size_t>(geometry.site_block);
        std::size_t named_failures = 0U;
        bool touched_site = false;
        for (std::size_t block = 0U; block < geometry.payload_blocks; ++block) {
            FakeAccess access{};
            cve::AccessParams params{};
            plant_site(access, plan, kWidth);
            access.corrupt_at = plan.payload_file_offset + block * kWidth;
            steps::HookRequest request{};
            request.access = &access;
            request.params = &params;
            request.plan = &plan;
            request.width = kWidth;
            const char *refusal = nullptr;
            if (!steps::write_hook(request, &refusal) && refusal != nullptr &&
                std::strcmp(refusal, "step-verify-failed") == 0) {
                ++named_failures;
            }
            for (const std::uint64_t at : access.offsets) {
                if (at == geometry.site_block) { touched_site = true; }
            }
            if (get_le32(access.memory, site + geometry.within) != plan.displaced_instruction) {
                touched_site = true;
            }
        }
        expect(named_failures == geometry.payload_blocks, "every corrupted payload block fails the hook by name");
        expect(!touched_site, "a failed block leaves the site block unwritten");
    }

    /** (c) 站点写成功之后立刻回读一致：最后一次读就是站点块，站点块其余字节原样保留。 */
    void the_site_is_read_back_after_it_is_written() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const auto geometry = geometry_of(plan, kWidth);
        const auto site = static_cast<std::size_t>(geometry.site_block);
        FakeAccess access{};
        cve::AccessParams params{};
        plant_site(access, plan, kWidth);
        /* A marker in the part of the block the hook must not touch (above the guard word). */
        const bool marker_fits = geometry.within >= 8U;
        if (marker_fits) { access.memory[site + geometry.within - 8U] = std::byte{0x5AU}; }
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWidth;
        const char *refusal = nullptr;
        const bool hooked = steps::write_hook(request, &refusal);
        if (!hooked) { std::printf("  (hook refusal: %s)\n", refusal == nullptr ? "-" : refusal); }
        expect(hooked, "the hook lands");
        std::uint32_t branch = 0U;
        expect(cve::hook::hook_branch(plan, branch) == cve::hook::SentryError::Ok, "the branch encodes");
        expect(get_le32(access.memory, site + geometry.within) == branch, "the site holds the branch word");
        expect(!access.read_offsets.empty() && access.read_offsets.back() == geometry.site_block,
               "the last read is the site block: the write-back was read back");
        expect(!access.offsets.empty() && access.offsets.back() == geometry.site_block,
               "and the site block is the last thing written before that read");
        if (marker_fits) {
            expect(access.memory[site + geometry.within - 8U] == std::byte{0x5AU},
                   "the rest of the site block is preserved");
        }
    }


    /** (d) restore 的最小步：站点两字按计划写回（guard 在站点前、displaced 在站点），写后回读一致。 */
    void the_restore_puts_both_words_back() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const auto geometry = geometry_of(plan, kWidth);
        const auto site = static_cast<std::size_t>(geometry.site_block);
        FakeAccess access{};
        cve::AccessParams params{};
        plant_site(access, plan, kWidth);
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWidth;
        const char *refusal = nullptr;
        expect(steps::write_hook(request, &refusal), "the hook lands before the restore");
        std::uint32_t branch = 0U;
        expect(cve::hook::hook_branch(plan, branch) == cve::hook::SentryError::Ok, "the branch encodes");
        expect(get_le32(access.memory, site + geometry.within) == branch, "the hook replaced the site word");
        const bool restored = steps::restore_hook(plan, request, &refusal);
        if (!restored) { std::printf("  (restore refusal: %s)\n", refusal == nullptr ? "-" : refusal); }
        expect(restored, "the restore completes");
        expect(get_le32(access.memory, site + geometry.within) == plan.displaced_instruction,
               "the displaced instruction is back at the site");
        expect(get_le32(access.memory, site + geometry.within - 4U) == plan.guard_instruction,
               "the guard word before it is the plan's guard");
        expect(!access.offsets.empty() && access.offsets.back() == geometry.site_block,
               "the restore writes the block that carries the site");
        expect(!access.read_offsets.empty() && access.read_offsets.back() == geometry.site_block,
               "and reads that block back after the write");
    }

    /** (e) restore 幂等：重复调用结果一致（同样的字节、同一块），没有 surface 时具名拒绝。 */
    void the_restore_is_idempotent() {
        const auto plan = cve::hook::device_sentry_plan();
        constexpr std::size_t kWidth = 16U;
        const auto geometry = geometry_of(plan, kWidth);
        const auto site = static_cast<std::size_t>(geometry.site_block);
        FakeAccess access{};
        cve::AccessParams params{};
        plant_site(access, plan, kWidth);
        steps::HookRequest request{};
        request.access = &access;
        request.params = &params;
        request.plan = &plan;
        request.width = kWidth;
        const char *refusal = nullptr;
        expect(steps::write_hook(request, &refusal), "the hook lands");
        expect(steps::restore_hook(plan, request, &refusal), "the first restore completes");
        std::byte snapshot[16] = {};
        std::memcpy(snapshot, access.memory + site, kWidth);
        const std::size_t writes = access.offsets.size();
        expect(steps::restore_hook(plan, request, &refusal), "a second restore is not an error");
        expect(std::memcmp(access.memory + site, snapshot, kWidth) == 0, "and it leaves the same bytes");
        expect(access.offsets.size() == writes + 1U && access.offsets.back() == geometry.site_block,
               "it re-writes the same block");
        expect(get_le32(access.memory, site + geometry.within) == plan.displaced_instruction &&
                       get_le32(access.memory, site + geometry.within - 4U) == plan.guard_instruction,
               "the two words are still the plan's");
        steps::HookRequest no_access{};
        expect(!steps::restore_hook(plan, no_access, &refusal) &&
                       std::strcmp(refusal, "step-access-missing") == 0,
               "a restore without a surface names itself");
    }

    void the_wait_reads_the_markers_in_order() {
        FakeWorld world{};
        g_world = &world;
        steps::WaitRequest request{};
        request.success_marker = "/dev/fake-success";
        request.failure_marker = "/dev/fake-failure";
        request.module_dir = "/sys/module/fake";
        request.budget_ms = 100U;
        request.poll_ms = 10U;
        request.exists = &world_exists;
        request.sleep_ms = &world_sleep;
        /* (a) 成功的标记在场 ⇒ 通过，一次都不睡。 */
        world.present[0] = "/dev/fake-success";
        world.present_count = 1U;
        expect(ghostlock::backend::cve_2026_43284::steps::wait_for_module(request) == nullptr,
               "a present success marker ends the wait");
        expect(world.slept_ms == 0U, "and it does not sleep first");
        /* (b) 失败标记在场 ⇒ 具名拒绝（哪怕成功标记也在）。 */
        world.present[0] = "/dev/fake-failure";
        world.present[1] = "/dev/fake-success";
        world.present_count = 2U;
        expect(std::strcmp(ghostlock::backend::cve_2026_43284::steps::wait_for_module(request),
                           "step-wait-failure-marker") == 0,
               "a failure marker wins over a success marker");
        /* (c) 什么都不在 ⇒ 花完预算后具名超时。 */
        world.present_count = 0U;
        world.slept_ms = 0U;
        expect(std::strcmp(ghostlock::backend::cve_2026_43284::steps::wait_for_module(request),
                           "step-wait-timeout") == 0,
               "an empty world times out");
        expect(world.slept_ms >= 100U, "and the budget was spent in polls");
        /* (d) 模块来过又走了、没有标记 ⇒ 具名失败（那次运行就是这么算的）。
         *     两个标记置空，世界只对模块路径答话：头一次「在场」，之后「不在」。 */
        request.success_marker = nullptr;
        request.failure_marker = nullptr;
        request.module_dir = "/sys/module/ghostlock-fake";
        request.budget_ms = 20U;
        world.present[0] = "/sys/module/ghostlock-fake";
        world.present[1] = nullptr;
        world.present_count = 1U;
        world.present_calls = 1;
        world.calls = 0;
        expect(std::strcmp(ghostlock::backend::cve_2026_43284::steps::wait_for_module(request),
                           "step-wait-module-gone") == 0,
               "a module that came and went is named");
        g_world = nullptr;
        request.exists = nullptr;
        expect(std::strcmp(ghostlock::backend::cve_2026_43284::steps::wait_for_module(request),
                           "step-wait-no-clock") == 0,
               "no clock is a named refusal");
    }

    void the_trigger_forks_and_returns() {
        /* delay 0：孙进程立刻退，父进程等一次 fork 就回来。 */
        expect(ghostlock::backend::cve_2026_43284::steps::trigger_orphan(0U) == nullptr,
               "the trigger completes a double fork");
    }
    void failures_are_named() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[8] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.width = 16U;
        const char *refusal = nullptr;
        access.refuse_write = true;
        /* 拒绝名带上访问层状态：测试只认前缀，后半段是设备上定位问题时读的那一项。 */
        expect(!steps::write_image_verified(request, &refusal) &&
                       std::strncmp(refusal, "step-write-refused", 18U) == 0,
               "a refused write names itself");
        access.refuse_write = false;
        access.corrupt = true;
        expect(!steps::write_image_verified(request, &refusal) && std::strcmp(refusal, "step-verify-failed") == 0,
               "a block that does not come back names itself");
        access.corrupt = false;
        steps::WriteRequest empty{};
        expect(!steps::write_image_verified(empty, &refusal) && std::strcmp(refusal, "step-access-missing") == 0,
               "no access is a named refusal");
    }
}  // namespace

int main() {
    std::printf("pagecache steps: names");
    names_map_both_ways();
    std::printf("pagecache steps: the shared block loop");
    an_image_is_written_block_by_block();
    std::printf("pagecache steps: the read-back sampling");
    the_sampling_rule_has_four_clauses();
    thirty_two_samples_one_block_in_thirty_two();
    zero_reads_every_block_back();
    a_sampled_block_that_does_not_come_back_fails_the_segment();
    a_failure_off_the_log_cadence_is_still_recorded();
    std::printf("pagecache steps: the journal and the rollback");
    a_verified_failure_rolls_the_journal_back_in_reverse();
    a_short_journal_overflows_and_keeps_writing();
    a_block_that_cannot_be_written_back_is_incomplete();
    a_clean_segment_rolls_back_nothing();
    std::printf("pagecache steps: the runner's journal buffer");
    the_runner_journals_and_rolls_back_in_reverse();
    a_short_runner_journal_overflows_and_keeps_writing();
    a_clean_runner_segment_rolls_back_nothing();
    an_incomplete_rollback_names_itself();
    std::printf("pagecache steps: the hook lands");
    the_hook_lands_and_verifies();
    std::printf("pagecache steps: the hook writes the payload first");
    the_payload_lands_before_the_site();
    a_failed_payload_block_never_writes_the_site();
    the_site_is_read_back_after_it_is_written();
    std::printf("pagecache steps: the restore");
    the_restore_puts_both_words_back();
    the_restore_is_idempotent();
    std::printf("pagecache steps: the wait and the trigger");
    the_wait_reads_the_markers_in_order();
    the_trigger_forks_and_returns();
    std::printf("pagecache steps: named failures");
    failures_are_named();
    if (g_failures != 0U) {
        std::printf("pagecache steps: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("pagecache steps: PASS (five names, one shared loop, named failures)\n");
    return 0;
}
