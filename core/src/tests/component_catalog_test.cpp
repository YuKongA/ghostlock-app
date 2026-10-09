/** @file tests/component_catalog_test.cpp
 *  Dispatch over chain rows, including the rows this build does not wire.
 *
 *  The chain catalogue is the authority for what runs and whether it is wired; this file only
 *  interprets it. Rows are passed in rather than looked up, which is what lets a row of our own with
 *  wired set to false reach the branch the production table may never reach -- a guard that cannot
 *  fail is not a guard. The shipped table is checked afterwards so the interpretation is known to
 *  agree with the rows that actually ship.
 */
#include <cstddef>
#include <cstdio>

#include "contract/chain_catalog.hpp"
#include "pipeline/component_catalog.hpp"

namespace {
    using ghostlock::contract::ChainSpec;
    using ghostlock::contract::kChainCatalog;
    using ghostlock::contract::kChainCount;
    using ghostlock::pipeline::backend_is_wired;
    using ghostlock::pipeline::dispatch_target_of;
    using ghostlock::pipeline::DispatchTarget;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A row of our own. The fields not under test are given values the catalogue also uses. */
    ChainSpec row_of(const char *backend, const char *step_set, const char *terminal, bool wired) {
        return ChainSpec{"test_chain", backend, "", "root_child", step_set, terminal, wired};
    }

    void the_wired_paths_map_to_their_targets() {
        expect(dispatch_target_of(row_of("cve_2026_43284", "pagecache", "umh_forward", true)) ==
                   DispatchTarget::Cve43284PageCache_UmhForward,
               "the page cache write that hands forward maps to its target");
        expect(dispatch_target_of(row_of("cve_2026_43499", "selinux_cred_seccomp", "root_child", true)) ==
                   DispatchTarget::Cve43499W1W3_RootChild,
               "the full privilege chain maps to the longer sequence");
        expect(dispatch_target_of(row_of("cve_2026_43499", "selinux_cred", "root_child", true)) ==
                   DispatchTarget::Cve43499W1W2_RootChild,
               "the same chain without seccomp maps to the shorter one");
    }

    void an_unwired_row_maps_to_nothing() {
        expect(dispatch_target_of(row_of("cve_2026_43499", "selinux_cred_seccomp", "root_child", false)) ==
                   DispatchTarget::None,
               "an unwired row maps to nothing: the branch the shipped table may not reach");
        expect(dispatch_target_of(row_of("cve_2026_43284", "pagecache", "umh_forward", false)) ==
                   DispatchTarget::None,
               "and so does an unwired page cache row");
    }

    void combinations_that_are_not_paths_map_to_nothing() {
        expect(dispatch_target_of(row_of("cve_2026_43499", "selinux_cred", "umh_forward", true)) ==
                   DispatchTarget::None,
               "the privilege chain does not hand forward");
        expect(dispatch_target_of(row_of("cve_2026_43499", "pagecache", "root_child", true)) ==
                   DispatchTarget::None,
               "and a page cache step set does not produce a rooted child");
        expect(dispatch_target_of(row_of("cve_2026_43284", "selinux_cred_seccomp", "root_child", true)) ==
                   DispatchTarget::None,
               "nor does the page cache backend run the privilege chain");
    }

    void the_shipped_table_agrees_with_the_interpretation() {
        for (std::size_t i = 0U; i < kChainCount; ++i) {
            const ChainSpec &row = kChainCatalog[i];
            if (dispatch_target_of(row) != DispatchTarget::None) {
                expect(row.wired, "a dispatched row is a wired row");
                expect(backend_is_wired(row.backend), "and its backend has a wired row too");
            }
        }
        expect(!backend_is_wired("cve_2026_64560"), "a declaration-only backend has no wired row");
        expect(!backend_is_wired(""), "and neither does the empty token");
        expect(backend_is_wired("cve_2026_43499"), "while the first backend has wired rows");
        std::printf("component catalog: %zu chain rows\n", kChainCount);
    }
}  // namespace

int main() {
    std::printf("component catalog: wired paths\n");
    the_wired_paths_map_to_their_targets();
    std::printf("component catalog: an unwired row\n");
    an_unwired_row_maps_to_nothing();
    std::printf("component catalog: combinations that are not paths\n");
    combinations_that_are_not_paths_map_to_nothing();
    std::printf("component catalog: the shipped table\n");
    the_shipped_table_agrees_with_the_interpretation();
    if (g_failures != 0U) {
        std::printf("component catalog: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("component catalog: PASS (maps, refuses, and agrees with the table)\n");
    return 0;
}
