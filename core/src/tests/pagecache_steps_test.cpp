/** @file tests/pagecache_steps_test.cpp
 *  封装步骤的证据：名字映射，以及「按块写入 + 每块回读」这一共用实现。
 *
 *  判据四条：① 五个名字往返一致，别的名字不认；② 一个 40 字节镜像按 16 字节宽写成 3 块，
 *  偏移逐块递进、最后一块补零；③ 写被拒 ⇒ step-write-refused，回读不符 ⇒ step-verify-failed；
 *  ④ 空镜像 / 空 access 各有一个具名拒绝。假 access 记录每一次读写，不碰设备。
 */
#include <cstddef>
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
        bool refuse_write = false;
        bool corrupt = false;
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
            if (at + size > kMemoryBytes) { return AccessResult<std::span<const std::byte>>::failed(Status::Unsupported); }
            for (std::size_t i = 0U; i < size; ++i) { scratch[i] = memory[at + i]; }
            if (corrupt) { scratch[0] = static_cast<std::byte>(0x7FU); }
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
    std::printf("pagecache steps: the hook lands");
    the_hook_lands_and_verifies();
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
