/* Host test for the Batch 5 backend contract: identity vs execution, the single
 * availability authority, and the declared registry against the catalogue. */

#include "route/backend_contract.hpp"
#include "route/pipeline.hpp"
#include "route/route_policy.hpp"
#include "session/backend/cve_2026_43499_backend.hpp"
#include "session/backend/cve_2026_64560_backend.hpp"
#include "session/root_child_frontend.hpp"

#include <cassert>
#include <cstdio>

using namespace ghostlock;

int32_t main(void) {
    using runtime::BackendExecution;
    using runtime::BackendIdentity;

    /* Identity: both declared backends carry a stable id. */
    static_assert(BackendIdentity<runtime::backend::Cve2026_43499>);
    static_assert(BackendIdentity<runtime::backend::Cve2026_64560>);

    /* Execution: the available backend provides steps for every catalogued
     * middleware; the placeholder provides none. */
    static_assert(BackendExecution<session::backend::Cve2026_43499Policy, route::SelectPolicy>);
    static_assert(BackendExecution<session::backend::Cve2026_43499Policy, route::TcpPolicy>);
    static_assert(BackendExecution<session::backend::Cve2026_43499Policy, route::MulticastPolicy>);
    static_assert(!BackendExecution<session::backend::Cve2026_64560Policy, route::SelectPolicy>);
    static_assert(!BackendExecution<session::backend::Cve2026_64560Policy, route::TcpPolicy>);
    static_assert(!BackendExecution<session::backend::Cve2026_64560Policy, route::MulticastPolicy>);

    /* Identity and execution policy name the same backend. */
    static_assert(session::backend::Cve2026_43499Policy::kind ==
                  runtime::backend::Cve2026_43499::kind);
    static_assert(session::backend::Cve2026_64560Policy::kind ==
                  runtime::backend::Cve2026_64560::kind);

    /* Pipeline is the composition entry: it enforces the backend execution
     * contract and wires the middleware policy to its dispatch target. */
    using SelectPipeline = runtime::Pipeline<session::frontend::RootChildPolicy,
                                             session::backend::Cve2026_43499Policy,
                                             route::SelectPolicy>;
    using TcpPipeline = runtime::Pipeline<session::frontend::RootChildPolicy,
                                          session::backend::Cve2026_43499Policy,
                                          route::TcpPolicy>;
    using MulticastPipeline = runtime::Pipeline<session::frontend::RootChildPolicy,
                                                session::backend::Cve2026_43499Policy,
                                                route::MulticastPolicy>;
    static_assert(SelectPipeline::catalogued && TcpPipeline::catalogued &&
                  MulticastPipeline::catalogued);
    static_assert(SelectPipeline::target == runtime::DispatchTarget::SelectStack);
    static_assert(TcpPipeline::target == runtime::DispatchTarget::TcpZerocopy);
    static_assert(MulticastPipeline::target == runtime::DispatchTarget::MulticastWaiter);

    /* Registry walks the declared identities; availability is only ever the
     * catalogue's answer (no identity/policy carries an available state). */
    int32_t known = 0;
    runtime::for_each_backend([&]<class B>() {
        known++;
        assert(runtime::backend_name(B::kind) != "");
    });
    assert(known == 2);
    assert(runtime::backend_available(runtime::backend::Cve2026_43499::kind));
    assert(!runtime::backend_available(runtime::backend::Cve2026_64560::kind));
    assert(runtime::backend_name(runtime::backend::Cve2026_64560::kind) == "cve_2026_64560");

    puts("backend_contract_test: ok");
    return 0;
}
