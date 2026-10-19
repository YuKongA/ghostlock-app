/** @file tests/handle_registry_test.cpp
 *  句柄表的规则（审查第 2 点）：销毁先关闭、回收后作废、槽复用换代、答案成对取回。
 *
 *  这些规则以前埋在 JNI 外壳里，没有测试；抽出来之后它们在宿主上逐条可验。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "boundary/protocol/handle_registry.hpp"

namespace {
    using ghostlock::jni::CallCode;
    using ghostlock::jni::HandleRegistry;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool same(const char *lhs, const char *rhs) { return std::strcmp(lhs, rhs) == 0; }

    /** 一个记账用的假会话：记住自己被关了几次、关掉之后还答不答。 */
    struct FakeBackend final {
        int shutdowns = 0;
        bool closed = false;

        [[nodiscard]] CallCode shutdown() noexcept {
            ++shutdowns;
            closed = true;
            return CallCode::Ok;
        }

        [[nodiscard]] CallCode touch() noexcept {
            return closed ? CallCode::Closed : CallCode::Ok;
        }

        [[nodiscard]] const char *refusal() const noexcept {
            return closed ? "session-closed" : "";
        }
    };

    void create_use_and_retire() {
        HandleRegistry<FakeBackend> table;
        const std::uint64_t handle = table.create();
        expect(handle > 0U, "a fresh handle is positive");
        expect(table.live_count() == 1U, "one session is live");
        expect(table.with_answer(handle, [](FakeBackend &backend) { return backend.touch(); }).code ==
                   CallCode::Ok,
               "a call on a live session answers ok");

        expect(table.retire(handle), "retire succeeds");
        expect(table.live_count() == 0U, "and the table is empty again");
        expect(table.retire(handle) == false, "retiring twice says so");
    }

    void destroy_runs_shutdown_first() {
        HandleRegistry<FakeBackend> table;
        const std::uint64_t handle = table.create();
        /* 调用方没有 shutdown，直接 destroy：设备侧资源也必须被放掉。 */
        expect(table.retire(handle), "retire succeeds");
        const auto outcome =
            table.with_answer(handle, [](FakeBackend &backend) { return backend.touch(); });
        expect(outcome.code == CallCode::StaleHandle, "the handle is stale afterwards");
        expect(same(outcome.refusal, "stale-handle"), "and says so by name");
    }

    void reuse_bumps_the_generation() {
        HandleRegistry<FakeBackend> table;
        const std::uint64_t first = table.create();
        expect(table.retire(first), "the first session retires");
        const std::uint64_t second = table.create();
        expect(second != first, "the recycled slot gets a new handle");
        expect((second & 0xFFFFFFFFU) == (first & 0xFFFFFFFFU), "same slot index");
        expect((second >> 32U) == ((first >> 32U) + 1U), "next generation");
        expect(table.with_answer(first, [](FakeBackend &backend) { return backend.touch(); }).code ==
                   CallCode::StaleHandle,
               "the old handle never comes back to life");
        expect(table.with_answer(second, [](FakeBackend &backend) { return backend.touch(); }).code ==
                   CallCode::Ok,
               "the new one works");
    }

    void the_answer_carries_the_call_s_name() {
        HandleRegistry<FakeBackend> table;
        const std::uint64_t handle = table.create();
        static_cast<void>(table.with_answer(handle, [](FakeBackend &backend) {
            return backend.shutdown();
        }));
        const auto outcome = table.with_answer(handle, [](FakeBackend &backend) {
            return backend.touch();
        });
        expect(outcome.code == CallCode::Closed, "a closed session answers closed");
        expect(same(outcome.refusal, "session-closed"), "and the name travels with the code");
    }

    void two_sessions_are_independent() {
        HandleRegistry<FakeBackend> table;
        const std::uint64_t left = table.create();
        const std::uint64_t right = table.create();
        expect(left != right, "two sessions");
        expect(table.live_count() == 2U, "both live");
        expect(table.retire(left), "the left one retires");
        expect(table.live_count() == 1U, "the right one survives");
        expect(table.with_answer(right, [](FakeBackend &backend) { return backend.touch(); }).code ==
                   CallCode::Ok,
               "and still answers");
    }
}  // namespace

int main() {
    std::printf("registry: create/use/retire\n");
    create_use_and_retire();
    std::printf("registry: destroy closes first\n");
    destroy_runs_shutdown_first();
    std::printf("registry: reuse bumps the generation\n");
    reuse_bumps_the_generation();
    std::printf("registry: the answer carries the name\n");
    the_answer_carries_the_call_s_name();
    std::printf("registry: two sessions\n");
    two_sessions_are_independent();
    if (g_failures != 0U) {
        std::printf("registry: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("registry: PASS (lifecycle, staleness, reuse, paired answers)\n");
    return 0;
}
