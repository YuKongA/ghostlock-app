#pragma once
/** @file contract/chain_catalog.hpp
 *
 *  KEY NOTE (measured, 2026-10-08): the selection key is NOT
 *  (backend, route, terminal). Four rows collide on that triple --
 *  mcast_rootchild(w1w3) vs mcast_shizuku(w1w2), the same pair for pselect and
 *  tcp, and pagecache_rootchild vs pagecache_shizuku -- and in the last pair the
 *  step set collides too (both pagecache) while the terminal column reads
 *  root_child for both. The distinguishing column is PATH, whose own declaration
 *  calls it the user's choice: so a chain is resolved by its TOKEN (chain_of),
 *  which is one to one with the combination catalogue, and the terminal column is
 *  derived from the path rather than chosen. Evidence, for the record:
 *    COLLIDE mcast_rootchild(w1w3) vs mcast_shizuku(w1w2) : backend=cve_2026_43499 route=multicast_waiter terminal=root_child
 *    COLLIDE pselect_rootchild(w1w3) vs pselect_shizuku(w1w2) : backend=cve_2026_43499 route=select_stack terminal=root_child
 *    COLLIDE tcp_rootchild(w1w3) vs tcp_shizuku(w1w2) : backend=cve_2026_43499 route=tcp_zerocopy terminal=root_child
 *    COLLIDE rootchild(pagecache) vs shizuku(pagecache) : backend=cve_2026_43284 route= terminal=root_child
 *  (These two rows were named pagecache_rootchild and pagecache_shizuku before the
 *  tokens were aligned with the combination manifest; see the pin below.)
 *  (probe compiled with the tree's headers, four lines printed, exit 0.)
 *
 *  The chains a selection can name: a token for a whole path through the tree.
 *
 *  A selection is one token, and the token stands for an entire chain -- the
 *  backend, the route inside it, what the path does with the child, the steps it
 *  runs and the terminal that finishes it. Only the tokens below are ever
 *  accepted; anything else is refused with the token echoed back, so this table
 *  is also the answer to what this build can be asked to do at all.
 *
 *  This is the finer table: kind.hpp's kCombinations says whether a backend and
 *  route run together at all, and every row here must be one of those. A row
 *  whose backend, route, step set or terminal the other catalogues do not declare
 *  does not compile.
 */
#include <cstddef>
#include <string_view>

#include "contract/kind.hpp"
#include "contract/op_registry.hpp" // the op tokens this table binds to step sets
#include "contract/step_catalog.hpp"
#include "profile/route_catalog.hpp"

namespace ghostlock::contract {
    /** One chain a token names. */
    struct ChainSpec final {
        /** The token a selection names. */
        const char *token;
        /** The backend that carries this chain. */
        const char *backend;
        /** The route inside that backend, or an empty string when the backend
         *  has no route axis. */
        const char *route;
        /** What the path does with the child: the user's choice. */
        const char *path;
        /** The step set the path runs, as the step catalogue spells it. */
        const char *step_set;
        /** The terminal that finishes the chain. */
        const char *terminal;
        /** Whether this build wires the chain end to end. A false row parses and
         *  is refused by the selection gate, never run silently. */
        bool wired;
    };

    /** The whitelist: twelve tokens, seven of them wired. */
    inline constexpr ChainSpec kChainCatalog[] = {
        {"mcast_rootchild", "cve_2026_43499", "multicast_waiter", "root_child", "w1w3",
         "root_child", true},
        {"pselect_rootchild", "cve_2026_43499", "select_stack", "root_child", "w1w3",
         "root_child", true},
        {"tcp_rootchild", "cve_2026_43499", "tcp_zerocopy", "root_child", "w1w3",
         "root_child", true},
        {"mcast_shizuku", "cve_2026_43499", "multicast_waiter", "shizuku", "w1w2",
         "root_child", true},
        {"pselect_shizuku", "cve_2026_43499", "select_stack", "shizuku", "w1w2",
         "root_child", true},
        {"tcp_shizuku", "cve_2026_43499", "tcp_zerocopy", "shizuku", "w1w2",
         "root_child", true},
        {"mcast_umh", "cve_2026_43499", "multicast_waiter", "umh", "w1w3", "umh_forward",
         false},
        {"pselect_umh", "cve_2026_43499", "select_stack", "umh", "w1w3", "umh_forward",
         false},
        {"tcp_umh", "cve_2026_43499", "tcp_zerocopy", "umh", "w1w3", "umh_forward",
         false},
        {"umh", "cve_2026_43284", "", "umh", "pagecache", "umh_forward", true},
        {"rootchild", "cve_2026_43284", "", "root_child", "pagecache", "root_child",
         false},
        {"shizuku", "cve_2026_43284", "", "shizuku", "pagecache", "root_child",
         false},
    };
    inline constexpr std::size_t kChainCount = sizeof(kChainCatalog) / sizeof(kChainCatalog[0]);

    /** A terminal token the component catalogue offers. */
    [[nodiscard]] constexpr bool terminal_token_is_offered(std::string_view token) noexcept {
        for (std::size_t i = 0U; i < kComponentCount; ++i) {
            if (kComponents[i].kind == ComponentKind::Frontend &&
                std::string_view(kComponents[i].token) == token) {
                return true;
            }
        }
        return false;
    }

    /** A backend token the component catalogue offers. */
    [[nodiscard]] constexpr bool backend_is_declared(std::string_view token) noexcept {
        for (std::size_t i = 0U; i < kComponentCount; ++i) {
            if (kComponents[i].kind == ComponentKind::Backend &&
                std::string_view(kComponents[i].token) == token) {
                return true;
            }
        }
        return false;
    }

    /** True when every row is reachable by its token and no two rows share one, and
     *  when the full selection key -- backend, route, path and terminal -- names
     *  exactly one row. The pair without path is NOT unique (see the note above),
     *  which is why the key includes it and why a caller resolves by token. */
    [[nodiscard]] constexpr bool tokens_are_unique() noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            std::size_t token_matches = 0U;
            std::size_t key_matches = 0U;
            for (std::size_t j = 0U; j < kChainCount; ++j) {
                const std::string_view a_token(kChainCatalog[i].token);
                const std::string_view b_token(kChainCatalog[j].token);
                if (a_token == b_token) {
                    ++token_matches;
                }
                if (std::string_view(kChainCatalog[i].backend) == std::string_view(kChainCatalog[j].backend) &&
                    std::string_view(kChainCatalog[i].route) == std::string_view(kChainCatalog[j].route) &&
                    std::string_view(kChainCatalog[i].path) == std::string_view(kChainCatalog[j].path) &&
                    std::string_view(kChainCatalog[i].terminal) == std::string_view(kChainCatalog[j].terminal)) {
                    ++key_matches;
                }
            }
            if (token_matches != 1U || key_matches != 1U) {
                return false;
            }
        }
        return true;
    }
    static_assert(tokens_are_unique(),
                  "every chain is reachable by its token, and the full selection key names one row");

    /** One op token, and the step set the plan side means by it. This binding is
     *  a declaration rather than a naming coincidence: the tokens and the step sets
     *  are two vocabularies, and the assertion below fails if either side renames
     *  something without the other. It is plan-side only -- a script never spells
     *  these tokens (see op_registry.hpp's note) -- and it exists so that
     *  pipeline::plan_gate can say which steps an op asked for without inferring
     *  anything from a name. */
    struct OpStepSetBinding final {
        const char *op_token;
        const char *step_set;
    };
    inline constexpr OpStepSetBinding kOpStepSetBindings[] = {
        {"window.w1w3", "w1w3"},
        {"window.w1w2", "w1w2"},
        {"window.pagecache", "pagecache"},
    };
    inline constexpr std::size_t kOpStepSetBindingCount =
        sizeof(kOpStepSetBindings) / sizeof(kOpStepSetBindings[0]);

    /** Every bound op token is in the registry, and every bound step set is one a
     *  chain row runs: a binding to something that does not exist reads as a plan
     *  decision that could never be carried out. */
    [[nodiscard]] constexpr bool bindings_are_declared() noexcept {
        for (std::size_t i = 0U; i < kOpStepSetBindingCount; ++i) {
            const std::string_view op(kOpStepSetBindings[i].op_token);
            const std::string_view step_set(kOpStepSetBindings[i].step_set);
            bool op_found = false;
            bool step_set_found = false;
            for (std::size_t j = 0U; j < kOpCount; ++j) {
                if (std::string_view(kOpCatalog[j].token) == op) {
                    op_found = true;
                }
            }
            for (std::size_t j = 0U; j < kChainCount; ++j) {
                if (std::string_view(kChainCatalog[j].step_set) == step_set) {
                    step_set_found = true;
                }
            }
            if (!op_found || !step_set_found) {
                return false;
            }
        }
        return true;
    }
    static_assert(bindings_are_declared(),
                  "an op step-set binding must name a declared op and a step set some chain runs");

    /** The same assertion, read the other way: an op this build can drive must have
     *  a binding, or the gate could only guess which steps it means, and no step set
     *  may be bound twice, or two ops would mean one thing. */
    [[nodiscard]] constexpr bool bindings_cover_the_drivable_ops() noexcept {
        for (std::size_t i = 0U; i < kOpCount; ++i) {
            /* Availability is the registry's own column: an op declared but not wired
             *  (window.w1w3_umh) is rejected by the gate by name and needs no binding,
             *  which is why the test is this flag rather than a non-empty route mask. */
            if (!kOpCatalog[i].available) {
                continue;
            }
            const std::string_view token(kOpCatalog[i].token);
            std::size_t bound = 0U;
            for (std::size_t b = 0U; b < kOpStepSetBindingCount; ++b) {
                if (std::string_view(kOpStepSetBindings[b].op_token) == token) {
                    ++bound;
                }
            }
            if (bound != 1U) {
                return false;
            }
        }
        for (std::size_t b = 0U; b < kOpStepSetBindingCount; ++b) {
            const std::string_view step_set(kOpStepSetBindings[b].step_set);
            std::size_t seen = 0U;
            for (std::size_t c = 0U; c < kOpStepSetBindingCount; ++c) {
                if (std::string_view(kOpStepSetBindings[c].step_set) == step_set) {
                    ++seen;
                }
            }
            if (seen != 1U) {
                return false;
            }
        }
        return true;
    }
    static_assert(bindings_cover_the_drivable_ops(),
                  "a drivable op needs exactly one binding, and a step set is bound once");

    /** The two spellings of the step sets: what the combination manifest says, and
     *  what this table says. The manifest's spellings are the ones on the wire, so
     *  the difference is a fact to be declared rather than remembered: a map entry
     *  is what makes "the same step set" checkable instead of a naming coincidence. */
    struct StepSpelling final {
        const char *manifest_steps;
        const char *chain_step_set;
    };
    inline constexpr StepSpelling kStepSpellings[] = {
        {"w1_w3", "w1w3"},
        {"w1_w2", "w1w2"},
        {"pagecache_write", "pagecache"},
    };
    inline constexpr std::size_t kStepSpellingCount = sizeof(kStepSpellings) / sizeof(kStepSpellings[0]);

    /** The two spellings of a path, same reasoning as the step sets above. */
    struct PathSpelling final {
        const char *manifest_path;
        const char *chain_path;
    };
    inline constexpr PathSpelling kPathSpellings[] = {
        {"rootchild", "root_child"},
        {"shizuku", "shizuku"},
        {"umh", "umh"},
    };
    inline constexpr std::size_t kPathSpellingCount = sizeof(kPathSpellings) / sizeof(kPathSpellings[0]);

    /** Every step set and every path a chain row uses must be explained by the maps
     *  above, and every map entry must be used: an unexplained spelling is a column
     *  that could drift without anything noticing. */
    [[nodiscard]] constexpr bool derived_columns_are_explained() noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            bool steps_explained = false;
            bool path_explained = false;
            for (std::size_t m = 0U; m < kStepSpellingCount; ++m) {
                if (std::string_view(kChainCatalog[i].step_set) ==
                    std::string_view(kStepSpellings[m].chain_step_set)) {
                    steps_explained = true;
                }
            }
            for (std::size_t m = 0U; m < kPathSpellingCount; ++m) {
                if (std::string_view(kChainCatalog[i].path) ==
                    std::string_view(kPathSpellings[m].chain_path)) {
                    path_explained = true;
                }
            }
            if (!steps_explained || !path_explained) {
                return false;
            }
        }
        for (std::size_t m = 0U; m < kStepSpellingCount; ++m) {
            bool used = false;
            for (std::size_t i = 0U; i < kChainCount; ++i) {
                if (std::string_view(kChainCatalog[i].step_set) ==
                    std::string_view(kStepSpellings[m].chain_step_set)) {
                    used = true;
                }
            }
            if (!used) {
                return false;
            }
        }
        for (std::size_t m = 0U; m < kPathSpellingCount; ++m) {
            bool used = false;
            for (std::size_t i = 0U; i < kChainCount; ++i) {
                if (std::string_view(kChainCatalog[i].path) ==
                    std::string_view(kPathSpellings[m].chain_path)) {
                    used = true;
                }
            }
            if (!used) {
                return false;
            }
        }
        return true;
    }
    static_assert(derived_columns_are_explained(),
                  "every derived spelling a chain uses must be in the maps, and every map entry used");

    /** The tokens the combination manifest carries, in its order. The wire and the
     *  App speak THESE spellings, so this table's tokens are pinned to them: two
     *  tables describing one selection must not drift, and a rename on either side
     *  now fails to compile rather than missing at run time. The derived columns
     *  (path, step set) keep this table's own spellings -- rootchild vs root_child,
     *  w1_w3 vs w1w3, pagecache_write vs pagecache -- and the columns that are
     *  literally shared (backend, terminal) are asserted equal below. */
    inline constexpr const char *kCombinationTokens[] = {
        "mcast_rootchild", "pselect_rootchild", "tcp_rootchild",
        "mcast_shizuku",   "pselect_shizuku",   "tcp_shizuku",
        "mcast_umh",       "pselect_umh",       "tcp_umh",
        "umh",             "rootchild",         "shizuku",
    };
    inline constexpr std::size_t kCombinationTokenCount =
        sizeof(kCombinationTokens) / sizeof(kCombinationTokens[0]);
    static_assert(kCombinationTokenCount == kChainCount,
                  "the combination manifest and this table describe the same selections");

    [[nodiscard]] constexpr bool tokens_match_the_combination_manifest() noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const std::string_view a(kChainCatalog[i].token);
            const std::string_view b(kCombinationTokens[i]);
            if (a.size() != b.size() || a != b) {
                return false;
            }
        }
        return true;
    }
    static_assert(tokens_match_the_combination_manifest(),
                  "this table's tokens must be the combination manifest's, in the same order");

    /** The row a token names, or nullptr when no row does. This is the resolution
     *  entry: the token comes from the combination catalogue, so a plan and a
     *  selection cannot disagree about which chain is meant. */
    [[nodiscard]] constexpr const ChainSpec *chain_of(std::string_view token) noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const std::string_view declared(kChainCatalog[i].token);
            if (declared.size() == token.size() && declared == token) {
                return &kChainCatalog[i];
            }
        }
        return nullptr;
    }

    /** The row a selection resolves to: the backend and terminal it names, the
     *
     *  Legacy entry, kept only while its test still drives it: it cannot distinguish
     *  the 43284 pair (rootchild and shizuku share backend, route, terminal and step
     *  set, and differ in path), so it is not what a plan resolves through. The
     *  resolver uses chain_of(token) -- see the key note at the top of this file.
     *  route it runs (none for a chain without a route axis) and the step set its
     *  steps spell out. A selection that matches no row is not a chain here. */
    [[nodiscard]] constexpr const ChainSpec *chain_for(std::string_view backend,
                                                       std::string_view route,
                                                       std::string_view terminal,
                                                       std::string_view step_set) noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const ChainSpec &row = kChainCatalog[i];
            if (std::string_view(row.backend) != backend || std::string_view(row.route) != route ||
                std::string_view(row.terminal) != terminal ||
                std::string_view(row.step_set) != step_set) {
                continue;
            }
            return &row;
        }
        return nullptr;
    }

    /* ---- what must hold, checked at compile time ---- */

    /** Every row names declared parts, its step set has an order, and the
     *  backend and route together are a combination the coarser table wires. */
    [[nodiscard]] constexpr bool chains_are_declared() noexcept {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const ChainSpec &row = kChainCatalog[i];
            if (row.token == nullptr || row.token[0U] == '\0' || !backend_is_declared(row.backend)) {
                return false;
            }
            if (step_set_order(std::string_view(row.step_set)) == nullptr) {
                return false;
            }
            if (!terminal_token_is_offered(std::string_view(row.terminal))) {
                return false;
            }
            const std::size_t position = row.route[0U] == '\0'
                                             ? profile::kNoRoute
                                             : profile::route_position(std::string_view(row.route));
            if (row.route[0U] != '\0' && position == profile::kNoRoute) {
                return false;
            }
            if (!combination_available(std::string_view(row.backend), position)) {
                return false;
            }
        }
        return true;
    }

    static_assert(chains_are_declared(),
                  "a chain names a part the catalogues do not declare, or a route its backend is not wired for");
}  // namespace ghostlock::contract
