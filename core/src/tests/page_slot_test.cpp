/** @file tests/page_slot_test.cpp
 *  The one-shot page slot of the W2 fast repair (page_slot.hpp ✓) has to fail loudly:
 *  a second stash, a second activate and an empty stash must all be refused, because a
 *  slot that silently accepts twice would hand the route a page the caller already reused.
 */
#include "backend/cve_2026_43499/memory/page_slot.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
    std::size_t g_failures = 0U;

    void expect(const bool condition, const char *what) {
        if (!condition) {
            ++g_failures;
            std::printf(" page slot: FAIL %s\n", what);
        }
    }

    void the_slot_hands_a_page_out_once() {
        using ghostlock::backend::cve_2026_43499::memory::PageSlot;
        std::uint8_t page[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
        PageSlot slot{};
        expect(!slot.staged(), "an empty slot is not staged");
        expect(slot.stash(page, sizeof(page)), "the first stash is accepted");
        expect(slot.staged(), "a stashed slot is staged");
        expect(!slot.stash(page, sizeof(page)), "a second stash is refused");
        const std::uint8_t *out = nullptr;
        std::size_t length = 0U;
        expect(slot.activate(out, length), "the first activate succeeds");
        expect(out == page && length == sizeof(page), "the activate hands out the stashed page");
        expect(!slot.staged(), "the slot is empty after the activate");
        expect(!slot.activate(out, length), "a second activate is refused");
        slot.discard();
        expect(!slot.staged(), "discard on an empty slot is harmless");
        expect(slot.stash(page, sizeof(page)), "the slot takes a page again after discard");
        slot.discard();
    }

    void nothing_empty_is_ever_stashed() {
        using ghostlock::backend::cve_2026_43499::memory::PageSlot;
        std::uint8_t page[4] = {};
        PageSlot slot{};
        expect(!slot.stash(nullptr, 4U), "a null page is refused");
        expect(!slot.stash(page, 0U), "a zero-length page is refused");
        expect(!slot.staged(), "the refusals left the slot empty");
        const std::uint8_t *out = nullptr;
        std::size_t length = 99U;
        expect(!slot.activate(out, length), "an empty slot cannot be activated");
        expect(out == nullptr && length == 99U, "a refused activate leaves its outputs alone");
    }
} // namespace

int main() {
    std::printf("page slot: the one-shot W2 repair slot");
    the_slot_hands_a_page_out_once();
    nothing_empty_is_ever_stashed();
    if (g_failures != 0U) {
        std::printf("page slot: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page slot: PASS\n");
    return 0;
}
