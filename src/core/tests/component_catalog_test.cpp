/* Host test for the component catalog: stable ids, availability and the
 * catalogued (backend, terminal) pairs. Route is backend-internal, so Auto
 * never enters the selection. */

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
    using pipeline::TerminalKind;

    assert(pipeline::terminal_available(TerminalKind::RootChild));
    assert(!pipeline::terminal_available(TerminalKind::UmhForward));
    assert(pipeline::backend_available(BackendKind::Cve2026_43499));
    assert(!pipeline::backend_available(BackendKind::Cve2026_64560));
    assert(!pipeline::backend_available(BackendKind::Cve2026_31431));
    assert(!pipeline::backend_available(BackendKind::Cve2026_43503));
    assert(!pipeline::backend_available(BackendKind::Cve2026_23274));

    for (MiddlewareKind kind : {MiddlewareKind::TcpZerocopy, MiddlewareKind::SelectStack,
                                MiddlewareKind::MulticastWaiter}) {
        assert(pipeline::middleware_available(kind));
    }
    assert(!pipeline::middleware_available(MiddlewareKind::Auto));

    assert(pipeline::selection_supported({TerminalKind::RootChild, BackendKind::Cve2026_43499}));
    assert(!pipeline::selection_supported({TerminalKind::UmhForward, BackendKind::Cve2026_43499}));
    assert(!pipeline::selection_supported({TerminalKind::RootChild, BackendKind::Cve2026_64560}));

    /* combination_supported is THE dispatch authority: exactly one pair. */
    const TerminalKind terminals[] = {TerminalKind::RootChild, TerminalKind::UmhForward};
    const BackendKind backends[] = {
        BackendKind::Cve2026_43499, BackendKind::Cve2026_64560, BackendKind::Cve2026_31431,
        BackendKind::Cve2026_43503, BackendKind::Cve2026_23274};
    int32_t catalogued = 0;
    for (TerminalKind t : terminals) {
        for (BackendKind b : backends) {
            const pipeline::ComponentSelection s{t, b};
            if (pipeline::combination_supported(s)) {
                catalogued++;
                assert(pipeline::selection_supported(s));
            }
            assert((pipeline::dispatch_target(s) != pipeline::DispatchTarget::None) ==
                   pipeline::combination_supported(s));
        }
    }
    assert(catalogued == 1);
    assert(pipeline::combination_supported({TerminalKind::RootChild, BackendKind::Cve2026_43499}));
    assert(!pipeline::combination_supported({TerminalKind::UmhForward, BackendKind::Cve2026_43499}));
    assert(!pipeline::combination_supported({TerminalKind::RootChild, BackendKind::Cve2026_64560}));

    assert(pipeline::dispatch_target({TerminalKind::RootChild, BackendKind::Cve2026_43499}) ==
           pipeline::DispatchTarget::Cve43499_RootChild);
    assert(pipeline::dispatch_target({TerminalKind::UmhForward, BackendKind::Cve2026_43499}) ==
           pipeline::DispatchTarget::None);
    assert(pipeline::dispatch_target({TerminalKind::RootChild, BackendKind::Cve2026_64560}) ==
           pipeline::DispatchTarget::None);

    static_assert(pipeline::dispatch_target_of(TerminalKind::RootChild, BackendKind::Cve2026_43499) ==
                  pipeline::DispatchTarget::Cve43499_RootChild);
    static_assert(pipeline::dispatch_target_of(TerminalKind::UmhForward, BackendKind::Cve2026_43499) ==
                  pipeline::DispatchTarget::None);
    static_assert(pipeline::dispatch_target_of(TerminalKind::RootChild, BackendKind::Cve2026_64560) ==
                  pipeline::DispatchTarget::None);

    assert(pipeline::terminal_name(TerminalKind::RootChild) == "root_child");
    assert(pipeline::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(pipeline::backend_name(BackendKind::Cve2026_64560) == "cve_2026_64560");
    assert(pipeline::middleware_name(MiddlewareKind::MulticastWaiter) == "multicast_waiter");
    assert(pipeline::middleware_name(MiddlewareKind::Auto) == "auto");

    puts("component_catalog_test: ok");
    return 0;
}
