/** @file tests/argument_names_test.cpp
 *  The reduction from a script argument name to a plan value.
 *
 *  ADR-0016. What matters here is not that a known name resolves but that everything else does not,
 *  and that a refusal leaves nothing behind: a miss must not write a value, because a zero that
 *  looks like an answer is the failure this table exists to prevent. The bound is checked from both
 *  sides too -- at the longest name and one past it -- so the scan cannot silently stop matching
 *  something the table does hold.
 */
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "script/argument_names.hpp"

namespace {
    using ghostlock::script::ArgumentNameRefusal;
    using ghostlock::script::argument_name_refusal_text;
    using ghostlock::script::kArgumentNameCount;
    using ghostlock::script::kArgumentNames;
    using ghostlock::script::longest_argument_name;
    using ghostlock::script::resolve_argument_name;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void every_name_in_the_table_resolves() {
        for (std::size_t i = 0U; i < kArgumentNameCount; ++i) {
            std::uint64_t value = 0U;
            const ArgumentNameRefusal r = resolve_argument_name(kArgumentNames[i].name, value);
            expect(r == ArgumentNameRefusal::Resolved, "a name in the table resolves");
            expect(value == kArgumentNames[i].value, "and yields the number the table carries");
        }
    }

    void an_unknown_name_is_refused_and_writes_nothing() {
        std::uint64_t value = 4242U;
        const ArgumentNameRefusal r = resolve_argument_name("not-in-the-table", value);
        expect(r == ArgumentNameRefusal::Unknown, "an unknown name is refused by name");
        expect(value == 4242U, "and the output is untouched: a refusal leaves no value behind");
        std::uint64_t empty_value = 7U;
        expect(resolve_argument_name("", empty_value) == ArgumentNameRefusal::Unknown,
               "the empty name is not a name");
        expect(empty_value == 7U, "and it writes nothing either");
        std::printf("argument names: refusal text %s\n",
                    argument_name_refusal_text(ArgumentNameRefusal::Unknown));
    }

    void the_bound_holds_from_both_sides() {
        const std::size_t longest = longest_argument_name();
        expect(longest > 0U, "the table has a longest name");
        std::uint64_t value = 0U;
        /* Exactly the longest length, with different text: long enough to matter, and unknown. */
        const std::string at_bound_text(longest, 'z');
        const std::string_view at_bound(at_bound_text);
        expect(resolve_argument_name(at_bound, value) == ArgumentNameRefusal::Unknown,
               "text of the longest length that is not in the table is still unknown");
        const std::string past_bound_text(longest + 1U, 'z');
        const std::string_view past_bound(past_bound_text);
        expect(resolve_argument_name(past_bound, value) == ArgumentNameRefusal::Unknown,
               "and one past the longest is unknown too");
    }

    void the_table_is_consistent_with_itself() {
        for (std::size_t i = 0U; i < kArgumentNameCount; ++i) {
            expect(!kArgumentNames[i].name.empty(), "no entry is unnamed");
            for (std::size_t j = i + 1U; j < kArgumentNameCount; ++j) {
                expect(kArgumentNames[i].name != kArgumentNames[j].name,
                       "and no two entries share a name, or the first would shadow the second");
                expect(kArgumentNames[i].value != kArgumentNames[j].value,
                       "nor a value, or two names would mean one thing");
            }
        }
    }
}  // namespace

int main() {
    std::printf("argument names: the table\n");
    every_name_in_the_table_resolves();
    std::printf("argument names: a miss\n");
    an_unknown_name_is_refused_and_writes_nothing();
    std::printf("argument names: the bound\n");
    the_bound_holds_from_both_sides();
    std::printf("argument names: consistency\n");
    the_table_is_consistent_with_itself();
    if (g_failures != 0U) {
        std::printf("argument names: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("argument names: PASS (resolves, refuses by name, leaves nothing behind)\n");
    return 0;
}
