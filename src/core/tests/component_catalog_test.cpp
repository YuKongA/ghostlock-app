/* Host test for the Batch 3 component catalog: stable ids, availability and
 * selection rejection. The orchestrator reuses the profile route enum for the
 * middleware, so Auto must never be selectable. */

#include "route/backend_policy.hpp"
#include "route/component_catalog.hpp"
#include "route/frontend_contract.hpp"
#include "route/pipeline.hpp"

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

    /* combination_supported is THE dispatch authority. Every admitted tuple
     * must also pass the per-id pre-check, and the current catalogue admits
     * exactly root_child x cve_2026_43499 x {tcp, select, multicast}. */
    const FrontendKind frontends[] = {FrontendKind::RootChild, FrontendKind::UmhForward};
    const BackendKind backends[] = {BackendKind::Cve2026_43499, BackendKind::Cve2026_64560};
    const MiddlewareKind middlewares[] = {MiddlewareKind::TcpZerocopy, MiddlewareKind::SelectStack,
                                          MiddlewareKind::MulticastWaiter, MiddlewareKind::Auto};
    int32_t catalogued = 0;
    for (FrontendKind f : frontends) {
        for (BackendKind b : backends) {
            for (MiddlewareKind m : middlewares) {
                const runtime::ComponentSelection s{f, b, m};
                if (runtime::combination_supported(s)) {
                    catalogued++;
                    assert(runtime::selection_supported(s));
                }
            }
        }
    }
    assert(catalogued == 3);
    assert(runtime::combination_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::TcpZerocopy}));
    assert(runtime::combination_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::SelectStack}));
    assert(runtime::combination_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::MulticastWaiter}));
    assert(!runtime::combination_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_43499, MiddlewareKind::Auto}));
    assert(!runtime::combination_supported(
        {FrontendKind::UmhForward, BackendKind::Cve2026_43499, MiddlewareKind::TcpZerocopy}));
    assert(!runtime::combination_supported(
        {FrontendKind::RootChild, BackendKind::Cve2026_64560, MiddlewareKind::TcpZerocopy}));

    /* Names are stable for diagnostics. */
    assert(runtime::frontend_name(FrontendKind::RootChild) == "root_child");
    assert(runtime::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(runtime::middleware_name(MiddlewareKind::MulticastWaiter) == "multicast_waiter");
    assert(runtime::middleware_name(MiddlewareKind::Auto) == "auto");

    puts("component_catalog_test: ok");
    return 0;
}
