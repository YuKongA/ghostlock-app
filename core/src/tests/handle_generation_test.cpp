/** @file tests/handle_generation_test.cpp
 *  A6-3 evidence: the typed handle table (A6 baseline item 6).
 *
 *  Sections: 1. acquire, use, release, and the generation that moves on so the
 *  value the script still holds can never be accepted again; 2. a second handle
 *  for the same kind is a conflict; 3. anything the host never issued is stale,
 *  including a slot outside the table; 4. the table refuses to grow past its
 *  bound.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "script/handle_table.hpp"

namespace {
    using ghostlock::script::Handle;
    using ghostlock::script::HandleRefusal;
    using ghostlock::script::HandleTable;
    using ghostlock::script::handle_refusal_name;
    using ghostlock::script::kMaxHandles;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void expect_refusal(HandleRefusal refusal, const char *name, const char *what) {
        expect(refusal != HandleRefusal::None, what);
        expect(std::string_view(handle_refusal_name(refusal)) == std::string_view(name), what);
    }

    /* ---- 1: acquire, release, and a generation that moves on ---- */
    void generations_move_on() {
        HandleTable table;
        Handle first{};
        expect(table.acquire(7U, first) == HandleRefusal::None, "the first acquire succeeds");
        expect(table.live_count() == 1U, "one handle is live");
        expect(first.generation == 1U, "the first handle is generation one");
        expect(table.check(first) == HandleRefusal::None, "a live handle checks out");
        expect(table.release(first) == HandleRefusal::None, "release succeeds");
        expect(table.live_count() == 0U, "nothing is live after release");
        expect_refusal(table.check(first), "handle-stale",
                       "the released value is stale: release moved the generation");
        expect_refusal(table.release(first), "handle-stale", "releasing twice is stale");

        Handle second{};
        expect(table.acquire(7U, second) == HandleRefusal::None, "a later acquire succeeds");
        expect(second.slot == first.slot, "the freed slot is reused");
        expect(second.generation == first.generation + 1U,
               "the reused slot carries the next generation");
        expect(table.check(first) == HandleRefusal::Stale,
               "the older handle of the same slot stays stale");
        expect(table.check(second) == HandleRefusal::None, "the new handle is live");
    }

    /* ---- 2: two live handles for one kind conflict ---- */
    void a_second_handle_conflicts() {
        HandleTable table;
        Handle first{};
        Handle second{};
        expect(table.acquire(3U, first) == HandleRefusal::None, "the first acquire succeeds");
        expect_refusal(table.acquire(3U, second), "handle-conflict",
                       "a second live handle of the same kind is a conflict");
        expect(table.live_count() == 1U, "the refused acquire issued nothing");
        expect(table.release(first) == HandleRefusal::None, "release the first");
        expect(table.acquire(3U, second) == HandleRefusal::None,
               "after release the same kind can be acquired again");
    }

    /* ---- 3: what the host never issued cannot pass ---- */
    void nothing_forged_passes() {
        HandleTable table;
        Handle live{};
        expect(table.acquire(1U, live) == HandleRefusal::None, "acquire for the forgery checks");
        Handle wrong_kind = live;
        wrong_kind.kind = 2U;
        expect(table.check(wrong_kind) == HandleRefusal::Stale, "a wrong kind is stale");
        Handle wrong_generation = live;
        wrong_generation.generation = live.generation + 9U;
        expect(table.check(wrong_generation) == HandleRefusal::Stale, "a wrong generation is stale");
        Handle no_such_slot = live;
        no_such_slot.slot = static_cast<std::uint32_t>(kMaxHandles + 1U);
        expect(table.check(no_such_slot) == HandleRefusal::Stale, "a slot off the table is stale");
        Handle never_issued{};
        never_issued.kind = 1U;
        expect(table.check(never_issued) == HandleRefusal::Stale, "a zero slot is never issued");
    }

    /* ---- 4: the table does not grow past its bound ---- */
    void the_table_is_bounded() {
        HandleTable table;
        Handle handle{};
        for (std::size_t i = 0U; i < kMaxHandles; ++i) {
            expect(table.acquire(static_cast<std::uint32_t>(100U + i), handle) == HandleRefusal::None,
                   "acquire within the bound succeeds");
        }
        expect(table.live_count() == kMaxHandles, "the table holds its bound");
        expect_refusal(table.acquire(999U, handle), "budget-exceeded",
                       "one past the bound is refused by name");
        expect(table.live_count() == kMaxHandles, "the refused acquire changed nothing");
    }
}  // namespace

int main() {
    std::printf("handles: generations\n");
    generations_move_on();
    std::printf("handles: conflicts\n");
    a_second_handle_conflicts();
    std::printf("handles: forged values\n");
    nothing_forged_passes();
    std::printf("handles: bound\n");
    the_table_is_bounded();
    if (g_failures != 0U) {
        std::printf("handles: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("handles: PASS (generations + conflicts + stale + bounded)\n");
    return 0;
}
