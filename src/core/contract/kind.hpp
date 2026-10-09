#pragma once
/** @file contract/kind.hpp
 *  A1: the neutral component-kind vocabulary and the two availability questions.
 *
 *  Scope of this header (and its explicit non-scope):
 *   - it names the four component kinds and answers availability;
 *   - it introduces NO handle table, NO handle type and NO lifetime semantics:
 *     ADR-0010 (Lua <-> C++ resource model) is still Proposed, so the handle
 *     model stays unimplemented until that ADR is approved.
 *
 *  Single authority: the kind tokens are defined HERE and nowhere else; route
 *  tokens are defined once in profile/route_catalog.hpp and are only referenced
 *  here (by its derived position constants), never restated.
 *
 *  Two questions, deliberately separate (they are not the same fact):
 *   - selection availability: may the app offer this component in this build?
 *     (a component can be selectable while its combination is not wired)
 *   - combination availability: will this backend x route pair actually run?
 *     (a wired combination requires both the backend and the route to be wired)
 *  The static_asserts at the bottom prove the two tables are distinct, so the
 *  two questions cannot silently collapse into one.
 *
 *  Numbers policy (Q2 rule 1): this file contains no geometry, offset, timing
 *  or tuning value. Kind ordinals and boolean flags are structure; the route
 *  wire values live, with their rationale, in profile/route_catalog.hpp.
 */
#include <cstddef>
#include <string_view>

#include "profile/route_catalog.hpp"

namespace ghostlock::contract {
    /** The four component kinds of the design family. */
    enum class ComponentKind : unsigned char {
        Backend = 0,
        Frontend = 1,
        Stepset = 2,
        Route = 3,
    };

    /** One kind row: the enum value and its token. */
    struct KindEntry final {
        ComponentKind kind;
        const char *token;
    };

    /** Kind vocabulary -- the only place these tokens are spelled. */
    inline constexpr KindEntry kKinds[] = {
        {ComponentKind::Backend, "backend"},
        {ComponentKind::Frontend, "frontend"},
        {ComponentKind::Stepset, "stepset"},
        {ComponentKind::Route, "route"},
    };
    inline constexpr std::size_t kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

    /** constexpr text equality (no <cstring> in a contract header). */
    [[nodiscard]] constexpr bool kind_same_text(const char *a, const char *b) noexcept {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    /** One offered component: its kind, its token and whether it is selectable. */
    struct ComponentEntry final {
        ComponentKind kind;
        std::string_view token;
        bool selectable;
    };

    /** What this build offers for selection (route rows are NOT listed here:
     *  routes come from profile/route_catalog.hpp, the single authority). */
    inline constexpr ComponentEntry kComponents[] = {
        {ComponentKind::Backend, "cve_2026_43499", true},
        {ComponentKind::Backend, "cve_2026_43284", true},
        {ComponentKind::Backend, "cve_2026_64560", false},
        {ComponentKind::Backend, "cve_2026_31431", false},
        {ComponentKind::Backend, "cve_2026_43503", false},
        {ComponentKind::Backend, "cve_2026_23274", false},
        {ComponentKind::Frontend, "root_child", true},
        {ComponentKind::Frontend, "umh_forward", true},
        {ComponentKind::Frontend, "shizuku", false},
        {ComponentKind::Stepset, "selinux_cred_seccomp", true},
        {ComponentKind::Stepset, "selinux_cred", true},
        {ComponentKind::Stepset, "pagecache", true},
    };
    inline constexpr std::size_t kComponentCount = sizeof(kComponents) / sizeof(kComponents[0]);

    /** One wired combination: a backend and a route position (kNoRoute = the
     *  backend has no route axis, e.g. the terminal-only endgame). */
    struct CombinationEntry final {
        std::string_view backend_token;
        std::size_t route_position;
        bool wired;
    };

    inline constexpr CombinationEntry kCombinations[] = {
        {"cve_2026_43499", profile::kRouteTcpZerocopy, true},
        {"cve_2026_43499", profile::kRouteSelectStack, true},
        {"cve_2026_43499", profile::kRouteMulticastWaiter, true},
        {"cve_2026_43284", profile::kNoRoute, true},
    };
    inline constexpr std::size_t kCombinationCount = sizeof(kCombinations) / sizeof(kCombinations[0]);

    /* ---- question 1: may the user select this component? ---- */
    [[nodiscard]] constexpr bool selection_available(ComponentKind kind, std::string_view token) noexcept {
        for (std::size_t i = 0; i < kComponentCount; ++i) {
            if (kComponents[i].kind == kind && kComponents[i].token == token) {
                return kComponents[i].selectable;
            }
        }
        return false;
    }

    /* ---- question 2: will this backend x route combination run? ---- */
    [[nodiscard]] constexpr bool combination_available(std::string_view backend_token,
                                                       std::size_t route_position) noexcept {
        for (std::size_t i = 0; i < kCombinationCount; ++i) {
            if (kCombinations[i].backend_token == backend_token &&
                kCombinations[i].route_position == route_position) {
                return kCombinations[i].wired;
            }
        }
        return false;
    }

    /** Routes are answered from the catalogue, not from a second list. */
    [[nodiscard]] constexpr bool route_selection_available(std::string_view route_token) noexcept {
        return profile::route_position(route_token) != profile::kNoRoute;
    }

    /* ---- compile-time proofs (each one can fail) ---- */
    [[nodiscard]] constexpr bool kind_tokens_are_unique() noexcept {
        for (std::size_t i = 0; i < kKindCount; ++i) {
            if (kKinds[i].token == nullptr || kKinds[i].token[0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kKindCount; ++j) {
                if (kind_same_text(kKinds[i].token, kKinds[j].token)) {
                    return false;
                }
            }
        }
        return true;
    }

    /** Every non-route component must name a kind that exists in kKinds. */
    [[nodiscard]] constexpr bool component_kinds_are_known() noexcept {
        for (std::size_t i = 0; i < kComponentCount; ++i) {
            bool known = false;
            for (std::size_t k = 0; k < kKindCount; ++k) {
                if (kComponents[i].kind == kKinds[k].kind) {
                    known = true;
                }
            }
            if (!known) {
                return false;
            }
        }
        return true;
    }

    /** Every combination route position must exist in the catalogue (or be the
     *  explicit "no route axis" sentinel). */
    [[nodiscard]] constexpr bool combination_routes_exist() noexcept {
        for (std::size_t i = 0; i < kCombinationCount; ++i) {
            const std::size_t pos = kCombinations[i].route_position;
            if (pos != profile::kNoRoute && pos >= profile::kRouteCatalogCount) {
                return false;
            }
        }
        return true;
    }

    /** The two questions are separate: frontends and stepsets are selectable but
     *  never appear as a combination axis, so availability cannot collapse. */
    [[nodiscard]] constexpr bool selection_and_combination_stay_separate() noexcept {
        bool selectable_non_axis = false;
        for (std::size_t i = 0; i < kComponentCount; ++i) {
            if (!kComponents[i].selectable) {
                continue;
            }
            if (kComponents[i].kind == ComponentKind::Frontend || kComponents[i].kind == ComponentKind::Stepset) {
                selectable_non_axis = true;
            }
        }
        return selectable_non_axis && kCombinationCount < kComponentCount;
    }

    static_assert(kKindCount == 4, "the kind vocabulary is frozen at four kinds");
    static_assert(kind_tokens_are_unique(), "kind tokens must be unique and non-empty");
    static_assert(component_kinds_are_known(), "every component must use a declared kind");
    static_assert(combination_routes_exist(), "every combination route must exist in the catalogue");
    static_assert(selection_and_combination_stay_separate(),
                  "selection availability and combination availability are different questions");
}  // namespace ghostlock::contract
