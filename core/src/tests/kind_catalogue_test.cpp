/** @file tests/kind_catalogue_test.cpp
 *  The two vocabularies added for the chain: step sets and terminals.
 *
 *  Both carry a list and a token function, and the file asserts that they agree. This test checks the
 *  same ground from outside the constexpr world, because an assertion that only runs at compile time
 *  cannot report which pair disagreed. The order is pinned too: it is the retired tree order, and a
 *  reordering that changed which value a stored number means should stop something.
 */
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "contract/kind.hpp"

namespace {
    using ghostlock::contract::kStepSetCount;
    using ghostlock::contract::kStepSets;
    using ghostlock::contract::kTerminalCount;
    using ghostlock::contract::kTerminals;
    using ghostlock::contract::step_set_token;
    using ghostlock::contract::StepSetKind;
    using ghostlock::contract::terminal_token;
    using ghostlock::contract::TerminalKind;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_step_sets_are_the_ported_ones() {
        expect(static_cast<int>(StepSetKind::Unknown) == 0, "unknown is zero");
        expect(static_cast<int>(StepSetKind::PageCacheWrite) == 1, "then the page cache write");
        expect(static_cast<int>(StepSetKind::W1W2) == 2, "then the shorter privilege sequence");
        expect(static_cast<int>(StepSetKind::W1W3) == 3, "then the one that includes seccomp");
        expect(kStepSetCount == 4U, "the list holds three and the count includes unknown");
    }

    void the_terminals_are_the_ported_ones() {
        expect(static_cast<int>(TerminalKind::Unknown) == 0, "unknown is zero");
        expect(static_cast<int>(TerminalKind::RootChild) == 1, "then the rooted child");
        expect(static_cast<int>(TerminalKind::UmhForward) == 2, "then the forwarding one");
        expect(kTerminalCount == 3U, "the list holds two and the count includes unknown");
    }

    void every_listed_value_names_itself() {
        for (std::size_t i = 0U; i < kStepSetCount - 1U; ++i) {
            expect(!step_set_token(kStepSets[i]).empty(), "a listed step set has a token");
        }
        for (std::size_t i = 0U; i < kTerminalCount - 1U; ++i) {
            expect(!terminal_token(kTerminals[i]).empty(), "a listed terminal has a token");
        }
        expect(step_set_token(StepSetKind::Unknown).empty(), "and unknown spells nothing");
        expect(terminal_token(TerminalKind::Unknown).empty(), "as does unknown in the other list");
        std::printf("kind catalogue: steps %zu, terminals %zu\n", kStepSetCount, kTerminalCount);
    }

    void no_name_or_value_appears_twice() {
        for (std::size_t i = 0U; i < kStepSetCount - 1U; ++i) {
            for (std::size_t j = i + 1U; j < kStepSetCount - 1U; ++j) {
                expect(kStepSets[i] != kStepSets[j], "no two step sets share a value");
                expect(step_set_token(kStepSets[i]) != step_set_token(kStepSets[j]),
                       "nor a token, or one would shadow the other");
            }
        }
        for (std::size_t i = 0U; i < kTerminalCount - 1U; ++i) {
            for (std::size_t j = i + 1U; j < kTerminalCount - 1U; ++j) {
                expect(kTerminals[i] != kTerminals[j], "no two terminals share a value");
                expect(terminal_token(kTerminals[i]) != terminal_token(kTerminals[j]),
                       "nor a token");
            }
        }
    }
}  // namespace

int main() {
    std::printf("kind catalogue: the step sets\n");
    the_step_sets_are_the_ported_ones();
    std::printf("kind catalogue: the terminals\n");
    the_terminals_are_the_ported_ones();
    std::printf("kind catalogue: tokens\n");
    every_listed_value_names_itself();
    std::printf("kind catalogue: distinctness\n");
    no_name_or_value_appears_twice();
    if (g_failures != 0U) {
        std::printf("kind catalogue: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("kind catalogue: PASS (step sets and terminals, named and distinct)\n");
    return 0;
}
