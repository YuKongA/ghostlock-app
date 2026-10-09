/** @file tests/write_layout_test.cpp
 *  What a write is aimed at, and the two rules that come with it.
 *
 *  Sections: 1. the layout carries the value a preserving write needs and none
 *  for a leaf; 2. the two cases the retired tree pinned -- one page value that is
 *  accepted and one that is refused because its second byte is even; 3. a request
 *  that asks for nothing produces a layout that says so.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "memory/write_layout.hpp"

namespace {
    using ghostlock::memory::layout_accepts_page;
    using ghostlock::memory::layout_matches_request;
    using ghostlock::memory::LayoutInputs;
    using ghostlock::memory::WriteLayout;
    using ghostlock::memory::WriteMode;
    using ghostlock::memory::WriteRequest;
    using ghostlock::memory::write_layout;

    constexpr std::uint64_t kPageGood = 0xffffff8800210000ULL;
    constexpr std::uint64_t kPageEven = 0xffffff8800200000ULL;
    constexpr std::uint64_t kInitCred = 0xffffff802abfd588ULL;
    constexpr std::uint64_t kTarget = 0xffffff8000124000ULL;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    LayoutInputs inputs(std::uintptr_t page_value) {
        LayoutInputs made;
        made.page_value = page_value;
        made.credential_value = kInitCred;
        made.default_fops = 0x1111U;
        made.credential_fops = 0x2222U;
        made.pointer_bytes = sizeof(std::uintptr_t);
        return made;
    }

    WriteRequest request(WriteMode mode, bool preserve, std::uint64_t target) {
        WriteRequest made;
        made.target = target;
        made.mode = mode;
        made.preserve_child = preserve;
        return made;
    }

    /* ---- 1: what the layout carries ---- */
    void the_layout_carries_what_the_request_needs() {
        const WriteLayout leaf = write_layout(request(WriteMode::Zero, false, kTarget),
                                              inputs(kPageGood));
        expect(leaf.value == 0U, "a leaf write carries no value");
        expect(leaf.parent == kTarget - sizeof(std::uintptr_t),
               "and knows the word before its target");
        expect(leaf.fops == 0x1111U && !leaf.needs_credential_copy,
               "and points at the default object without copying a credential");

        const WriteLayout zero = write_layout(request(WriteMode::Zero, true, kTarget),
                                              inputs(kPageGood));
        expect(zero.value == kPageGood, "a preserving zero write carries the page value");

        const WriteLayout credential =
            write_layout(request(WriteMode::Credential, true, kTarget), inputs(kPageGood));
        expect(credential.value == kInitCred, "a credential write carries the alias");
        expect(credential.fops == 0x2222U && credential.needs_credential_copy,
               "and points at the credential object and asks for the copy");

        const WriteLayout tiny = write_layout(request(WriteMode::Zero, false, 4U), inputs(kPageGood));
        expect(tiny.parent == 0U, "a target too small to step back from does not wrap around");
    }

    /* ---- 2: the two cases the retired tree pinned ---- */
    void the_pinned_cases_hold() {
        const WriteRequest w1 = request(WriteMode::Zero, true, kTarget);
        const WriteLayout accepted = write_layout(w1, inputs(kPageGood));
        expect(layout_matches_request(w1, accepted), "a preserving write carries a value");
        expect(layout_accepts_page(w1, accepted),

               "and a page whose second byte is odd is accepted");
        const WriteLayout rejected = write_layout(w1, inputs(kPageEven));
        expect(!layout_accepts_page(w1, rejected),

               "while an even second byte is refused: it would clear the state field");
        /* The same layout with no value at all does not match a preserving request. */
        WriteLayout emptied = accepted;
        emptied.value = 0U;
        expect(!layout_matches_request(w1, emptied),

               "and a preserving write with no value does not match its request");
        const WriteRequest leaf = request(WriteMode::Zero, false, kTarget);
        WriteLayout carries = write_layout(leaf, inputs(kPageGood));
        expect(layout_matches_request(leaf, carries), "a leaf write carries none, so it matches");
        carries.value = kPageGood;
        expect(!layout_matches_request(leaf, carries),

               "and one that carries a value does not match a leaf request");
    }

    /* ---- 3: a request that asks for nothing ---- */
    void nothing_is_refused() {
        const WriteRequest disabled = request(WriteMode::Disabled, false, kTarget);
        const WriteLayout layout = write_layout(disabled, inputs(kPageGood));
        expect(!layout_matches_request(disabled, layout),
               "a disabled request matches no layout");
        expect(!layout_accepts_page(disabled, layout), "and is not accepted either");
        const WriteRequest nowhere = request(WriteMode::Zero, false, 0U);
        expect(write_layout(nowhere, inputs(kPageGood)).parent == 0U,

               "a write with no target has no parent word");
    }
}  // namespace

int main() {
    std::printf("write layout: what it carries\n");
    the_layout_carries_what_the_request_needs();
    std::printf("write layout: the pinned cases\n");
    the_pinned_cases_hold();
    std::printf("write layout: nothing\n");
    nothing_is_refused();
    if (g_failures != 0U) {
        std::printf("write layout: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("write layout: PASS (values chosen + the two rules + nothing refused)\n");
    return 0;
}
