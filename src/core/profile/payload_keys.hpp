#pragma once
/** @file profile/payload_keys.hpp
 *  The payload family of the document, declared once.
 *
 *  A payload carries four keys -- its bytes, the digest declared for them, the
 *  byte count, and what kind of deliverable it is -- and its name is not free:
 *  a name starts with a terminal token the tree already declares and may carry
 *  a suffix after an underscore. Nothing here restates those terminals: they
 *  come from the component catalogue, so a terminal added there is admissible
 *  here without a second edit, and one removed there stops being admissible.
 *
 *  The family is what makes an enumerated manifest possible: the digests are
 *  per name, but the names are bounded by the terminal catalogue, so a reader
 *  that refuses unknown paths can still accept this family by rule.
 */
#include <cstddef>
#include <string_view>

#include "contract/kind.hpp"

namespace ghostlock::profile {
    /** The four keys of one payload, spelled here and nowhere else. */
    inline constexpr const char *kPayloadKeys[] = {"bin", "sha256", "size", "kind"};
    inline constexpr std::size_t kPayloadKeyCount =
        sizeof(kPayloadKeys) / sizeof(kPayloadKeys[0]);

    /** The values .kind may take: a plan, a finishing script, or a module. */
    inline constexpr const char *kPayloadKinds[] = {"plan", "script", "module"};
    inline constexpr std::size_t kPayloadKindCount =
        sizeof(kPayloadKinds) / sizeof(kPayloadKinds[0]);

    /** A suffix is bounded: a name is a terminal token plus at most this many
     *  characters, which keeps every accepted name short enough to enumerate. */
    inline constexpr std::size_t kPayloadSuffixMax = 16U;

    [[nodiscard]] constexpr bool payload_text_is(const char *text,
                                                 std::string_view wanted) noexcept {
        if (text == nullptr) {
            return false;
        }
        std::size_t i = 0U;
        while (i < wanted.size() && text[i] == wanted[i]) {
            ++i;
        }
        return i == wanted.size() && text[i] == '\0';
    }

    /** True when the token names a terminal the catalogue offers, which is the
     *  set a payload name may start with. Only selectable rows count: a token
     *  that maps onto a terminal (a combination) is not a terminal itself. */
    [[nodiscard]] inline bool terminal_is_declared(std::string_view token) noexcept {
        for (std::size_t i = 0U; i < contract::kComponentCount; ++i) {
            if (contract::kComponents[i].kind != contract::ComponentKind::Frontend ||
                !contract::kComponents[i].selectable) {
                continue;
            }
            const std::string_view declared(contract::kComponents[i].token);
            if (declared.size() == token.size() && declared == token) {
                return true;
            }
        }
        return false;
    }

    /** The declared terminal a name starts with, or an empty view when none. */
    [[nodiscard]] inline std::string_view terminal_of(std::string_view name) noexcept {
        for (std::size_t i = 0U; i < contract::kComponentCount; ++i) {
            if (contract::kComponents[i].kind != contract::ComponentKind::Frontend ||
                !contract::kComponents[i].selectable) {
                continue;
            }
            const std::string_view declared(contract::kComponents[i].token);
            if (name.size() < declared.size() || name.substr(0U, declared.size()) != declared) {
                continue;
            }
            if (name.size() == declared.size() || name[declared.size()] == '_') {
                return declared;
            }
        }
        return {};
    }

    [[nodiscard]] inline bool suffix_is_valid(std::string_view suffix) noexcept {
        if (suffix.empty() || suffix.size() > kPayloadSuffixMax) {
            return false;
        }
        for (const char c : suffix) {
            const bool digit = c >= '0' && c <= '9';
            const bool lower = c >= 'a' && c <= 'z';
            if (!digit && !lower && c != '_') {
                return false;
            }
        }
        return true;
    }

    /** The rule in one place: a selectable terminal, an underscore, a suffix.
     *  The suffix is required: a bare terminal would give one concept two shapes
     *  and would fall outside the family a manifest can enumerate. */
    [[nodiscard]] inline bool payload_name_is_valid(std::string_view name) noexcept {
        const std::string_view terminal = terminal_of(name);
        if (terminal.empty() || name.size() <= terminal.size() + 1U) {
            return false;
        }
        return suffix_is_valid(name.substr(terminal.size() + 1U));
    }

    /** True when the key is one of the four. */
    [[nodiscard]] inline bool payload_key_is_declared(std::string_view key) noexcept {
        for (std::size_t i = 0U; i < kPayloadKeyCount; ++i) {
            if (std::string_view(kPayloadKeys[i]) == key) {
                return true;
            }
        }
        return false;
    }

    /** True when the value is one the kind key may carry. */
    [[nodiscard]] inline bool payload_kind_is_declared(std::string_view value) noexcept {
        for (std::size_t i = 0U; i < kPayloadKindCount; ++i) {
            if (std::string_view(kPayloadKinds[i]) == value) {
                return true;
            }
        }
        return false;
    }

    /** backend.<id>.payload.<name>.<key> into out; false when it does not fit
     *  or the name or key is not admissible (nothing is emitted then). */
    [[nodiscard]] bool payload_path(char *out, std::size_t capacity,
                                    std::string_view backend, std::string_view name,
                                    std::string_view key) noexcept;
}  // namespace ghostlock::profile
