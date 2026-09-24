/* Host test for the Batch 3 component catalog: stable ids, availability and
 * selection rejection. The orchestrator reuses the profile route enum for the
 * middleware, so Auto must never be selectable. */

#include "route/component_catalog.hpp"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

int32_t main(void) {
    using runtime::BackendKind;
    using runtime::FrontendKind;
    using runtime::MiddlewareKind;

    assert(runtime::frontend_available(FrontendKind::RootChild));
    assert(!runtime::frontend_available(FrontendKind::UmhForward));
    assert(runtime::backend_available(BackendKind::Cve2026_43499));
    assert(!runtime::backend_available(BackendKind::Cve2026_64560));

    for (MiddlewareKind kind : {MiddlewareKind::TcpZerocopy, MiddlewareKind::SelectStack,
                                MiddlewareKind::MulticastWaiter}) {
        assert(runtime::middleware_available(kind));
    }
    assert(!runtime::middleware_available(MiddlewareKind::Auto));

    /* Only fully-available combinations are supported. */
    assert(runtime::selection_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::SelectStack}));
    assert(!runtime::selection_supported(
        {FrontendKind::UmhForward, BackendKind::Cve2026_43499, MiddlewareKind::SelectStack}));
    assert(!runtime::selection_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_64560, MiddlewareKind::SelectStack}));
    assert(!runtime::selection_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::Auto}));

    /* Names are stable for diagnostics. */
    assert(runtime::frontend_name(FrontendKind::RootChild) == "root_child");
    assert(runtime::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(runtime::middleware_name(MiddlewareKind::MulticastWaiter) == "multicast_waiter");
    assert(runtime::middleware_name(MiddlewareKind::Auto) == "auto");

    puts("component_catalog_test: ok");
    return 0;
}
