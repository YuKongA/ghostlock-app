/* Host test for the component catalog: stable ids, availability and the
 * sparse (backend, steps, terminal) catalogue. Route is backend-internal, so
 * Auto never enters the selection. */

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

int32_t main(void) {
    using contract::BackendKind;
    using pipeline::MiddlewareKind;
    using contract::StepSetKind;
    using contract::TerminalKind;

    assert(contract::terminal_available(TerminalKind::RootChild));
    assert(!contract::terminal_available(TerminalKind::UmhForward));
    assert(contract::backend_available(BackendKind::Cve2026_43499));
    assert(!contract::backend_available(BackendKind::Cve2026_64560));
    assert(!contract::backend_available(BackendKind::Cve2026_31431));
    assert(!contract::backend_available(BackendKind::Cve2026_43503));
    assert(!contract::backend_available(BackendKind::Cve2026_23274));
    assert(!contract::backend_available(BackendKind::Cve2026_43284));
    for (StepSetKind s : {StepSetKind::W1W2, StepSetKind::W1W3, StepSetKind::PageCacheWrite}) {
        assert(contract::stepset_available(s));
    }
    for (MiddlewareKind kind : {MiddlewareKind::TcpZerocopy, MiddlewareKind::SelectStack,
                                MiddlewareKind::MulticastWaiter}) {
        assert(pipeline::middleware_available(kind));
    }
    assert(!pipeline::middleware_available(MiddlewareKind::Auto));

    assert(contract::selection_supported({BackendKind::Cve2026_43499, StepSetKind::W1W3,
                                          TerminalKind::RootChild}));
    assert(!contract::selection_supported({BackendKind::Cve2026_64560, StepSetKind::W1W3,
                                           TerminalKind::RootChild}));
    assert(!contract::selection_supported({BackendKind::Cve2026_43284, StepSetKind::W1W3,
                                           TerminalKind::RootChild}));
    assert(!contract::selection_supported({BackendKind::Cve2026_43499, StepSetKind::W1W3,
                                           TerminalKind::UmhForward}));
    /* B5-8: the 43284 triple is catalogued (wired) but its backend is not
     * device-verified, so the availability gate stays false. */
    assert(!contract::selection_supported({BackendKind::Cve2026_43284,
                                           StepSetKind::PageCacheWrite,
                                           TerminalKind::UmhForward}));

    /* combination_supported is THE catalogue/dispatch authority: every wired
     * triple has a DispatchTarget, and only selection_supported() says whether it
     * may run. */
    const TerminalKind terminals[] = {TerminalKind::RootChild, TerminalKind::UmhForward};
    const BackendKind backends[] = {
        BackendKind::Cve2026_43499, BackendKind::Cve2026_64560, BackendKind::Cve2026_31431,
        BackendKind::Cve2026_43503, BackendKind::Cve2026_23274, BackendKind::Cve2026_43284};
    const StepSetKind stepsets[] = {StepSetKind::W1W2, StepSetKind::W1W3,
                                    StepSetKind::PageCacheWrite};
    int32_t catalogued = 0;
    for (BackendKind b : backends) {
        for (StepSetKind st : stepsets) {
            for (TerminalKind t : terminals) {
                const contract::ComponentSelection s{b, st, t};
                if (pipeline::combination_supported(s)) {
                    /* Catalogued/wired, not necessarily device-verified: the
                     * 43284 triple is wired for B5-8 coverage while
                     * selection_supported() keeps it fail-closed. */
                    ++catalogued;
                    assert(pipeline::dispatch_target(s) !=
                           pipeline::DispatchTarget::None);
                }
                assert((pipeline::dispatch_target(s) != pipeline::DispatchTarget::None) ==
                       pipeline::combination_supported(s));
            }
        }
    }
    assert(catalogued == 3);
    assert(pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W3, TerminalKind::RootChild}));
    assert(pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W2, TerminalKind::RootChild}));
    assert(pipeline::combination_supported(
        {BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
         TerminalKind::UmhForward}));
    assert(!pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W3, TerminalKind::UmhForward}));
    assert(!pipeline::combination_supported(
        {BackendKind::Cve2026_43284, StepSetKind::W1W3, TerminalKind::UmhForward}));
    assert(!pipeline::combination_supported(
        {BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
         TerminalKind::RootChild}));
    assert(!pipeline::combination_supported(
        {BackendKind::Cve2026_64560, StepSetKind::W1W3, TerminalKind::RootChild}));

    assert(pipeline::dispatch_target(
               {BackendKind::Cve2026_43499, StepSetKind::W1W3, TerminalKind::RootChild}) ==
           pipeline::DispatchTarget::Cve43499W1W3_RootChild);
    static_assert(pipeline::dispatch_target_of(
                      BackendKind::Cve2026_43499, StepSetKind::W1W3,
                      TerminalKind::RootChild) ==
                  pipeline::DispatchTarget::Cve43499W1W3_RootChild);
    static_assert(pipeline::dispatch_target_of(
                      BackendKind::Cve2026_43499, StepSetKind::W1W2,
                      TerminalKind::RootChild) ==
                  pipeline::DispatchTarget::Cve43499W1W2_RootChild);
    static_assert(pipeline::dispatch_target_of(
                      BackendKind::Cve2026_43499, StepSetKind::W1W3,
                      TerminalKind::UmhForward) ==
                  pipeline::DispatchTarget::None);
    /* B5-8: the 43284 triple has a dispatch target even though it stays
     * unavailable; dispatch reaches the wired branch, the orchestrator gate
     * rejects it before running. */
    assert(pipeline::dispatch_target(
               {BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
                TerminalKind::UmhForward}) ==
           pipeline::DispatchTarget::Cve43284PageCache_UmhForward);
    static_assert(pipeline::dispatch_target_of(
                      BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
                      TerminalKind::UmhForward) ==
                  pipeline::DispatchTarget::Cve43284PageCache_UmhForward);
    static_assert(pipeline::dispatch_target_of(
                      BackendKind::Cve2026_43284, StepSetKind::PageCacheWrite,
                      TerminalKind::RootChild) ==
                  pipeline::DispatchTarget::None);

    assert(pipeline::terminal_name(TerminalKind::RootChild) == "root_child");
    assert(pipeline::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(pipeline::backend_name(BackendKind::Cve2026_43284) == "cve_2026_43284");
    assert(pipeline::stepset_name(StepSetKind::W1W3) == "w1_w3");
    assert(pipeline::middleware_name(MiddlewareKind::MulticastWaiter) == "multicast_waiter");
    assert(pipeline::middleware_name(MiddlewareKind::Auto) == "auto");

    puts("component_catalog_test: ok");
    return 0;
}
