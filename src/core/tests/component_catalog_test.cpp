/* Host test for the component catalog: stable ids, availability and the
 * sparse (backend, steps, terminal) catalogue. Route is backend-internal, so
 * Auto never enters the selection. */

#include "pipeline/backend_policy.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/terminal_contract.hpp"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

int32_t main(void) {
    using pipeline::BackendKind;
    using pipeline::MiddlewareKind;
    using pipeline::StepSetKind;
    using pipeline::TerminalKind;

    assert(pipeline::terminal_available(TerminalKind::RootChild));
    assert(!pipeline::terminal_available(TerminalKind::UmhForward));
    assert(pipeline::backend_available(BackendKind::Cve2026_43499));
    assert(!pipeline::backend_available(BackendKind::Cve2026_64560));
    assert(!pipeline::backend_available(BackendKind::Cve2026_31431));
    assert(!pipeline::backend_available(BackendKind::Cve2026_43503));
    assert(!pipeline::backend_available(BackendKind::Cve2026_23274));
    assert(!pipeline::backend_available(BackendKind::Cve2026_43284));
    for (StepSetKind s : {StepSetKind::W1W2, StepSetKind::W1W3, StepSetKind::PageCacheWrite}) {
        assert(pipeline::stepset_available(s));
    }
    for (MiddlewareKind kind : {MiddlewareKind::TcpZerocopy, MiddlewareKind::SelectStack,
                                MiddlewareKind::MulticastWaiter}) {
        assert(pipeline::middleware_available(kind));
    }
    assert(!pipeline::middleware_available(MiddlewareKind::Auto));

    assert(pipeline::selection_supported({BackendKind::Cve2026_43499, StepSetKind::W1W3,
                                          TerminalKind::RootChild}));
    assert(!pipeline::selection_supported({BackendKind::Cve2026_64560, StepSetKind::W1W3,
                                           TerminalKind::RootChild}));
    assert(!pipeline::selection_supported({BackendKind::Cve2026_43284, StepSetKind::W1W3,
                                           TerminalKind::RootChild}));
    assert(!pipeline::selection_supported({BackendKind::Cve2026_43499, StepSetKind::W1W3,
                                           TerminalKind::UmhForward}));

    /* combination_supported is THE dispatch authority: exactly one triple. */
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
                const pipeline::ComponentSelection s{b, st, t};
                if (pipeline::combination_supported(s)) {
                    catalogued++;
                    assert(pipeline::selection_supported(s));
                }
                assert((pipeline::dispatch_target(s) != pipeline::DispatchTarget::None) ==
                       pipeline::combination_supported(s));
            }
        }
    }
    assert(catalogued == 2);
    assert(pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W3, TerminalKind::RootChild}));
    assert(pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W2, TerminalKind::RootChild}));
    assert(!pipeline::combination_supported(
        {BackendKind::Cve2026_43499, StepSetKind::W1W3, TerminalKind::UmhForward}));
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

    assert(pipeline::terminal_name(TerminalKind::RootChild) == "root_child");
    assert(pipeline::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(pipeline::backend_name(BackendKind::Cve2026_43284) == "cve_2026_43284");
    assert(pipeline::stepset_name(StepSetKind::W1W3) == "w1_w3");
    assert(pipeline::middleware_name(MiddlewareKind::MulticastWaiter) == "multicast_waiter");
    assert(pipeline::middleware_name(MiddlewareKind::Auto) == "auto");

    puts("component_catalog_test: ok");
    return 0;
}
