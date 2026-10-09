#pragma once
/** @file script/argument_names.hpp
 *  Names that may cross from a script argument into a plan, and what they stand for.
 *
 *  ADR-0016. A plan parameter is a name and a number, and a script argument may be a string; the
 *  reduction between them is this table and nothing more. Three properties, each of which the old
 *  tree's own constraints ask for: the table is a constant, so a script cannot grow it; the lookup is
 *  a scan of a constant, so no text is compared against anything a script controls; and a miss is a
 *  named refusal rather than a zero, so a silent default cannot pass for a resolved name.
 *
 *  The names are the ones the surface already carries as identifiers -- the kind fields of the write
 *  and copy rows and the terminal row. Nothing here invents vocabulary: every entry is a string the
 *  surface already uses for something else, which is why a miss is a refusal and not a guess.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::script {
    /** One name and the number the plan carries in its place. */
    struct ArgumentName final {
        std::string_view name;
        std::uint64_t value;
    };

    /** The names a string argument may reduce to. Bounded by construction: it is this list. */
    inline constexpr ArgumentName kArgumentNames[] = {
        {"handle", 1U},
        {"target", 2U},
        {"src", 3U},
        {"dst", 4U},
        {"terminal", 5U},
    };
    inline constexpr std::size_t kArgumentNameCount =
        sizeof(kArgumentNames) / sizeof(kArgumentNames[0]);

    /** Why a string argument did not reduce. */
    enum class ArgumentNameRefusal : unsigned char {
        Resolved = 0,
        /** Not in the table. The caller reports this; it does not become a zero. */
        Unknown = 1,
        /** Longer than any name in the table, so the scan cannot match one. */
        TooLong = 2,
    };

    /** The longest name the table holds, so a caller can bound what it offers. */
    [[nodiscard]] constexpr std::size_t longest_argument_name() noexcept {
        std::size_t longest = 0U;
        for (std::size_t i = 0U; i < kArgumentNameCount; ++i) {
            if (kArgumentNames[i].name.size() > longest) {
                longest = kArgumentNames[i].name.size();
            }
        }
        return longest;
    }

    /**
     *  Reduce one script argument name to the number a plan carries.
     *
     *  @param text The argument as the script gave it; not interpreted as anything else.
     *  @param out  The plan value, written only on success.
     *  @return Resolved, or the reason it was not. A miss leaves @p out untouched.
     *  @note  Comparison is against the table and against nothing else, and the scan stops at the
     *         first match, so the answer does not depend on what the script sent beyond its text.
     */
    [[nodiscard]] inline ArgumentNameRefusal resolve_argument_name(std::string_view text,
                                                                  std::uint64_t &out) noexcept {
        if (text.empty() || text.size() > longest_argument_name()) {
            return ArgumentNameRefusal::Unknown;
        }
        for (std::size_t i = 0U; i < kArgumentNameCount; ++i) {
            if (text.size() == kArgumentNames[i].name.size() &&
                text == kArgumentNames[i].name) {
                out = kArgumentNames[i].value;
                return ArgumentNameRefusal::Resolved;
            }
        }
        return ArgumentNameRefusal::Unknown;
    }

    /** The refusal as a name, for a diagnostic that has to say which one happened. */
    [[nodiscard]] constexpr const char *argument_name_refusal_text(ArgumentNameRefusal r) noexcept {
        switch (r) {
            case ArgumentNameRefusal::Resolved: return "resolved";
            case ArgumentNameRefusal::Unknown: return "argument-name-unknown";
            case ArgumentNameRefusal::TooLong: return "argument-name-too-long";
        }
        return "argument-name-unknown";
    }
}  // namespace ghostlock::script
