#pragma once
/** @file contract/chain_catalog.hpp
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
        {"pagecache_rootchild", "cve_2026_43284", "", "root_child", "pagecache", "root_child",
         false},
        {"pagecache_shizuku", "cve_2026_43284", "", "shizuku", "pagecache", "root_child",
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

    /** The row a token names, or nullptr when no row does. */
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
