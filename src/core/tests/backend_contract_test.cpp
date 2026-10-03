/* Host test for the backend/terminal contracts: identity vs execution, the
 * single availability authority, and the declared registry against the
 * catalogue. */

#include "pipeline/backend_contract.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_23274_backend.hpp"
#include "backend/cve_2026_31431_backend.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43503_backend.hpp"
#include "backend/cve_2026_64560_backend.hpp"
#include "terminal/root_child.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace ghostlock;

int32_t main(void) {
    using pipeline::BackendExecution;
    using pipeline::BackendIdentity;

    static_assert(BackendIdentity<pipeline::backend::Cve2026_43499>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_64560>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_31431>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_43503>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_23274>);

    /* The available backend provides the step entry; the placeholders do not. */
    static_assert(BackendExecution<backend::Cve2026_43499Policy>);
    static_assert(!BackendExecution<backend::Cve2026_64560Policy>);
    static_assert(!BackendExecution<backend::Cve2026_31431Policy>);
    static_assert(!BackendExecution<backend::Cve2026_43503Policy>);
    static_assert(!BackendExecution<backend::Cve2026_23274Policy>);

    static_assert(backend::Cve2026_43499Policy::kind ==
                  pipeline::backend::Cve2026_43499::kind);
    static_assert(backend::Cve2026_64560Policy::kind ==
                  pipeline::backend::Cve2026_64560::kind);

    /* Pipeline is the composition entry: one (backend, terminal) pair. */
    using RootChildPipeline = pipeline::Pipeline<backend::Cve2026_43499Policy,
                                                terminal::RootChildPolicy>;
    static_assert(RootChildPipeline::target == pipeline::DispatchTarget::Cve43499W1W3_RootChild);

    static_assert(pipeline::TerminalIdentity<terminal::RootChildPolicy>);
    static_assert(pipeline::TerminalExecution<terminal::RootChildPolicy>);
    static_assert(pipeline::TerminalIdentity<terminal::UmhForwardPolicy>);
    static_assert(!pipeline::TerminalExecution<terminal::UmhForwardPolicy>);

    /* Unified interface (R19/R20): every terminal declares Input + activation. */
    static_assert(std::is_same_v<terminal::RootChildPolicy::Input, terminal::RootedChild>);
    static_assert(std::is_base_of_v<terminal::TerminalInput, terminal::RootChildPolicy::Input>);
    static_assert(terminal::RootChildPolicy::activation == terminal::ActivationContext::Descendant);
    static_assert(std::is_same_v<terminal::UmhForwardPolicy::Input, terminal::UmhForwardInput>);
    static_assert(terminal::UmhForwardPolicy::activation ==
                  terminal::ActivationContext::KernelSpawned);

    int32_t known = 0;
    pipeline::for_each_backend([&]<class B>() {
        known++;
        assert(pipeline::backend_name(B::kind) != "");
    });
    assert(known == 5);
    int32_t terminals = 0;
    pipeline::for_each_terminal([&]<class F>() {
        terminals++;
        assert(pipeline::terminal_name(F::kind) != "");
    });
    assert(terminals == 2);
    assert(pipeline::backend_available(pipeline::backend::Cve2026_43499::kind));
    assert(!pipeline::backend_available(pipeline::backend::Cve2026_64560::kind));

    puts("backend_contract_test: ok");
    return 0;
}
