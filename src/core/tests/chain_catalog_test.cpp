/** @file tests/chain_catalog_test.cpp
 *  The chains a selection names, and that a selection resolves to one.
 *
 *  Sections: 1. the table has the twelve tokens the tree carried, seven wired;
 *  2. every row names declared parts and a route its backend is wired for;
 *  3. the two selections this build is meant to run resolve to a row, one per
 *  backend, and a selection that matches nothing does not.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/chain_catalog.hpp"

namespace {
    using ghostlock::contract::backend_is_declared;
    using ghostlock::contract::chain_for;
    using ghostlock::contract::chain_of;
    using ghostlock::contract::ChainSpec;
    using ghostlock::contract::chains_are_declared;
    using ghostlock::contract::kChainCatalog;
    using ghostlock::contract::kChainCount;
    using ghostlock::contract::step_set_order;
    using ghostlock::contract::terminal_token_is_offered;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /* ---- 1: the table itself ---- */
    void the_table_holds() {
        expect(kChainCount == 12U, "the tree carried twelve tokens");
        std::size_t wired = 0U;
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            if (kChainCatalog[i].wired) {
                ++wired;
            }
            for (std::size_t j = i + 1U; j < kChainCount; ++j) {
                expect(std::strcmp(kChainCatalog[i].token, kChainCatalog[j].token) != 0,
                       "no two tokens are the same");
            }
        }
        expect(wired == 7U, "seven of them are wired");
        expect(chains_are_declared(),
               "every row names declared parts (the compile-time check, read back)");
    }

    /* ---- 2: the parts of every row exist ---- */
    void the_parts_exist() {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const ChainSpec &row = kChainCatalog[i];
            expect(backend_is_declared(std::string_view(row.backend)), "the backend is declared");
            expect(terminal_token_is_offered(std::string_view(row.terminal)),
                   "the terminal is offered");
            const ghostlock::contract::StepSetOrder *order =
                step_set_order(std::string_view(row.step_set));
            expect(order != nullptr && order->count > 0U,
                   "the step set has a non-empty declared order");
            if (order != nullptr) {
                for (std::size_t s = 0U; s < order->count; ++s) {
                    expect(order->steps[s] != ghostlock::contract::StepId::Unknown,
                           "and every step in it is a real step");
                }
            }
        }
    }

    /* ---- 3: the selections resolve ---- */
    void the_selections_resolve() {
        /* What the device's document selects for 43499: multicast, root child,
         * and the steps w1 w2 w3 -- which the step catalogue spells w1w3. */
        const ChainSpec *mcast =
            chain_for("cve_2026_43499", "multicast_waiter", "root_child", "w1w3");
        expect(mcast != nullptr, "the 43499 multicast selection resolves");
        expect(mcast != nullptr && std::strcmp(mcast->token, "mcast_rootchild") == 0,
               "to the token the tree used");
        expect(mcast != nullptr && mcast->wired, "and it is wired");
        const ghostlock::contract::StepSetOrder *steps =
            mcast != nullptr ? step_set_order(std::string_view(mcast->step_set)) : nullptr;
        expect(steps != nullptr && steps->count == 3U &&
                   steps->steps[0U] == ghostlock::contract::StepId::W1 &&
                   steps->steps[2U] == ghostlock::contract::StepId::W3,
               "and its steps are w1, w2, w3 in that order");

        /* What the device's own export selects for 43284: no route, the umh
         * terminal, and a single page cache step. */
        const ChainSpec *umh = chain_for("cve_2026_43284", "", "umh_forward", "pagecache");
        expect(umh != nullptr, "the 43284 selection resolves");
        expect(umh != nullptr && umh->wired, "and it is wired");
        const ghostlock::contract::StepSetOrder *umh_steps =
            umh != nullptr ? step_set_order(std::string_view(umh->step_set)) : nullptr;
        expect(umh_steps != nullptr && umh_steps->count == 1U &&
                   umh_steps->steps[0U] == ghostlock::contract::StepId::PageCacheWrite,
               "and it runs the single page cache step");

        expect(chain_of(std::string_view("mcast_rootchild")) == mcast, "a token finds its own row");
        expect(chain_of(std::string_view("not_a_chain")) == nullptr,
               "a token the table does not carry finds nothing");
        /* w1w2 with multicast and a root child is the shizuku chain, so it is a
         * chain -- the two differ by their steps, not by their terminal. */
        const ChainSpec *shizuku = chain_for("cve_2026_43499", "multicast_waiter", "root_child", "w1w2");
        expect(shizuku != nullptr && std::strcmp(shizuku->path, "shizuku") == 0,
               "the two-step selection is the shizuku path, and it says so");
        expect(chain_for("cve_2026_43499", "multicast_waiter", "umh_forward", "w1w2") == nullptr,
               "a step set no row pairs with that backend, route and terminal is not a chain");
        const ChainSpec *planned = chain_for("cve_2026_43499", "tcp_zerocopy", "umh_forward", "w1w3");
        expect(planned != nullptr, "a planned row still resolves");
        expect(planned != nullptr && !planned->wired,
               "and it is marked unwired rather than silently runnable");
    }
}  // namespace

int main() {
    std::printf("chains: the table\n");
    the_table_holds();
    std::printf("chains: the parts\n");
    the_parts_exist();
    std::printf("chains: the selections\n");
    the_selections_resolve();
    if (g_failures != 0U) {
        std::printf("chains: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("chains: PASS (twelve tokens + declared parts + selections resolve)\n");
    return 0;
}
