/** @file tests/pagecache_steps_test.cpp
 *  封装步骤的证据：名字映射，以及「按块写入 + 每块回读」这一共用实现。
 *
 *  判据四条：① 五个名字往返一致，别的名字不认；② 一个 40 字节镜像按 16 字节宽写成 3 块，
 *  偏移逐块递进、最后一块补零；③ 写被拒 ⇒ step-write-refused，回读不符 ⇒ step-verify-failed；
 *  ④ 空镜像 / 空 access 各有一个具名拒绝。假 access 记录每一次读写，不碰设备。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"

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
        /** 只让第一次读该偏移失真：造「一次失败、重发后比中」的块，用来验抽样后的强制全查。 */
        std::uint64_t corrupt_once_at = kNoCorruption;
        bool corrupt_once_used = false;
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
            if (!corrupt_once_used && offset_of(parameters) == corrupt_once_at) {
                corrupt_once_used = true;
                scratch[0] = static_cast<std::byte>(0x7FU);
            }
            return AccessResult<std::span<const std::byte>>::ok(
                    std::span<const std::byte>(&scratch[0], size));
        }
        [[nodiscard]] AccessResult<std::size_t> write(
                std::uint64_t, std::span<const std::byte> in, contract::AccessChannel,
                const contract::KernelAccess::AccessMiscParameters &parameters) noexcept override {
            offsets.push_back(offset_of(parameters));
            widths.push_back(in.size());
            if (refuse_write) { return AccessResult<std::size_t>::failed(Status::Unsupported); }
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

    /** 抽样 (a)：首块、每 N 块、最后一块一定回读；其余块不回读 —— 读写次数就是证据。 */
    void sampling_reads_the_first_stride_and_last_block() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[5U * 16U] = {};
        for (std::size_t i = 0U; i < sizeof(image); ++i) { image[i] = static_cast<std::byte>(i + 1U); }
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x1000U;
        request.width = 16U;
        request.verify_every = 3U;
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal), "the image still goes in");
        expect(access.offsets.size() == 5U, "every block is still written");
        expect(access.read_offsets.size() == 3U, "three read-backs for five blocks at stride three");
        expect(access.read_offsets.size() == 3U && access.read_offsets[0] == 0x1000U &&
                       access.read_offsets[1] == 0x1030U && access.read_offsets[2] == 0x1040U,
               "the first block, every third block, and the last block are read back");
    }

    /** 抽样 (b)：任何一次失败尝试之后，下一块无论步长都全查。 */
    void a_failure_makes_the_next_block_read_back() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[40U * 16U] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x1000U;
        request.width = 16U;
        request.verify_every = 32U;
        /* 预算给足，让首块能重发一次；第一次回读失真，第二次比中。 */
        request.budget_ms = 100U;
        access.corrupt_once_at = 0x1000U;
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal), "the image lands after one retry");
        expect(access.offsets.size() == 41U, "the first block was sent twice, the rest once");
        bool next_read = false;
        for (const std::uint64_t at : access.read_offsets) {
            if (at == 0x1010U) { next_read = true; }
        }
        expect(next_read, "the block after the failed attempt is read back whatever the stride");
        expect(access.read_offsets.size() == 5U,
               "first block twice, next block once, the two sampled blocks, and no more");
    }

    /** 抽样 (c)：被抽到的那一块不符仍然是整段失败（失败即整段失败，语义未变）。 */
    void a_sampled_block_that_does_not_land_fails_the_segment() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[64U * 16U] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x1000U;
        request.width = 16U;
        request.verify_every = 32U;
        request.budget_ms = 0U;
        /* 第 32 块恰好在抽样点上（32 % 32 == 0）：它读到的不是写进去的字节。 */
        access.corrupt_at = 0x1200U;
        const char *refusal = nullptr;
        expect(!steps::write_image_verified(request, &refusal),
               "a sampled block that does not come back fails the whole segment");
        expect(refusal != nullptr && std::strcmp(refusal, "step-verify-failed") == 0,
               "and the refusal keeps its name");
    }

    /** 步长 0 = 不抽样：每块都回读（调用方的显式选择）。 */
    void a_zero_stride_reads_every_block() {
        FakeAccess access{};
        cve::AccessParams params{};
        std::byte image[3U * 16U] = {};
        steps::WriteRequest request{};
        request.access = &access;
        request.params = &params;
        request.image = std::span<const std::byte>(image, sizeof(image));
        request.offset = 0x1000U;
        request.width = 16U;
        request.verify_every = 0U;
        const char *refusal = nullptr;
        expect(steps::write_image_verified(request, &refusal), "the image goes in");
        expect(access.read_offsets.size() == 3U, "a zero stride reads back every block");
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
    std::printf("pagecache steps: read-back sampling");
    sampling_reads_the_first_stride_and_last_block();
    a_failure_makes_the_next_block_read_back();
    a_sampled_block_that_does_not_land_fails_the_segment();
    a_zero_stride_reads_every_block();
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
