/** @file tests/snitch_child_test.cpp
 *  One poke, and the loop that makes many.
 *
 *  The assertion that matters is that a poke does both things: the count moves, which the
 *  parent watches, and the word is touched, which is what the kernel hashes. A child that
 *  only counted would look healthy from the parent and produce no collision at all.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/snitch_child.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::ChildArgs;
    using ghostlock::backend::cve_2026_43499::leak::child_main;
    using ghostlock::backend::cve_2026_43499::leak::poke_once;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A landing that only has to answer the two calls a child makes. */
    struct FakeOps final {
        static std::size_t wakes;
        static std::uint32_t *last_word;
        static OpsResult futex_wake_private(std::uint32_t *word, std::int32_t) noexcept {
            ++wakes;
            last_word = word;
            return OpsResult{1, 0};
        }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return OpsResult{0, 0};
        }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept {
            return OpsResult{0, 0};
        }
        static std::size_t unmaps;
        static OpsResult unmap(void *, std::size_t) noexcept { ++unmaps; return OpsResult{0, 0}; }
        static OpsResult stop_child(std::uint64_t) noexcept { return OpsResult{0, 0}; }
        static MapResult map_shared(std::size_t) noexcept { return MapResult{nullptr, 0}; }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept { return ThreadResult{0U, 0}; }
        static OpsResult join_thread(unsigned long) noexcept { return OpsResult{0, 0}; }
    };
    std::size_t FakeOps::wakes = 0U;
    std::uint32_t *FakeOps::last_word = nullptr;
    std::size_t FakeOps::unmaps = 0U;

    void a_poke_does_both_things() {
        FakeOps::wakes = 0U;
        FakeOps::last_word = nullptr;
        std::uint32_t word = 0U;
        std::uint32_t counter = 0U;
        std::uint32_t stop = 0U;
        ChildArgs args;
        args.word = &word;
        args.counter = &counter;
        args.stop = &stop;
        args.amount = 3U;
        expect(poke_once<FakeOps>(args), "a poke with somewhere to poke succeeds");
        expect(counter == 3U, "the count the parent watches went up by the amount");
        expect(word == 1U, "and the word itself changed, which is the part the kernel hashes");
        expect(FakeOps::wakes == 1U && FakeOps::last_word == &word,
               "with one futex operation on that same word");
        expect(!poke_once<FakeOps>(ChildArgs{}), "a poke with no word is not a poke");
    }

    void the_loop_runs_until_it_is_asked_to_stop() {
        FakeOps::wakes = 0U;
        std::uint32_t word = 0U;
        std::uint32_t counter = 0U;
        std::uint32_t stop = 1U; /* already asked to stop */
        ChildArgs args;
        args.word = &word;
        args.counter = &counter;
        args.stop = &stop;
        child_main<FakeOps>(args);
        expect(counter == 0U, "a child told to stop before it starts does nothing");
        expect(word == 0U && FakeOps::wakes == 0U, "and touches nothing");
        /* With no stop word at all the loop is never entered: a child that cannot be told
         *  to stop is one that would run until it was killed, which is not what a caller
         *  who passed nothing asked for. */
        child_main<FakeOps>(ChildArgs{});
        expect(FakeOps::wakes == 0U, "and a child with no word to poke returns rather than spinning");
    }
}  // namespace

int main() {
    std::printf("snitch child: one poke\n");
    a_poke_does_both_things();
    std::printf("snitch child: the loop\n");
    the_loop_runs_until_it_is_asked_to_stop();
    if (g_failures != 0U) {
        std::printf("snitch child: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch child: PASS (a poke counts and touches, and the loop respects the stop word)\n");
    return 0;
}
