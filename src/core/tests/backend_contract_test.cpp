/* Host test for the backend/terminal contracts: identity vs execution, the
 * single availability authority, and the declared registry against the
 * catalogue. */

#include "contract/identity.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_23274_backend.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_31431_backend.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43503_backend.hpp"
#include "backend/cve_2026_64560_backend.hpp"
#include "terminal/root_child.hpp"
#include "terminal/terminal_input.hpp"
#include "terminal/umh_forward.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

using namespace ghostlock;

namespace {
    /* B5 contract test double (§5.6): a backend whose *only* terminal input is
     * UmhForwardInput. It declares run() and is never defined, instantiated or
     * linked; it exists solely to prove BackendExecution is parameterised on
     * the input type. No 43284 execution is implemented here. */
    struct UmhInputPlaceholderBackend final {
        static constexpr contract::BackendKind kind = contract::BackendKind::Cve2026_43284;

        [[nodiscard]] static profile::BindStatus state_from(
            session::CoreSession &, const profile::Document &);

        [[nodiscard]] static contract::StageResult run(
            session::CoreSession &, const char *, bool,
            terminal::UmhForwardInput &);
    };
} // namespace

int32_t main(void) {
    using contract::BackendExecution;
    using contract::BackendIdentity;

    static_assert(BackendIdentity<contract::backend::Cve2026_43499>);
    static_assert(BackendIdentity<contract::backend::Cve2026_64560>);
    static_assert(BackendIdentity<contract::backend::Cve2026_31431>);
    static_assert(BackendIdentity<contract::backend::Cve2026_43503>);
    static_assert(BackendIdentity<contract::backend::Cve2026_23274>);
    static_assert(BackendIdentity<contract::backend::Cve2026_43284>);

    /* The available backend provides the step entry; the placeholders do not. */
    static_assert(BackendExecution<backend::Cve2026_43499Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_64560Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_31431Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_43503Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_23274Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_43284Policy, terminal::RootedChild>);
    /* B5-7: the 43284 execution/state contracts land for its paired terminal
     * input while the backend stays uncatalogued (backend_available is false). */
    static_assert(BackendExecution<backend::Cve2026_43284Policy, terminal::UmhForwardInput>);

    /* R10 continuation: BackendExecution is parameterised on the terminal input
     * type. cve_2026_43499 only fills RootedChild, so it does not satisfy the
     * contract for UmhForwardInput; the test double proves the converse. */
    static_assert(!BackendExecution<backend::Cve2026_43499Policy, terminal::UmhForwardInput>);
    static_assert(BackendExecution<UmhInputPlaceholderBackend, terminal::UmhForwardInput>);
    static_assert(!BackendExecution<UmhInputPlaceholderBackend, terminal::RootedChild>);

    /* B2: the available backend provides its state contract; the placeholders,
     * which never reach Pipeline, carry none. */
    using contract::BackendState;
    static_assert(BackendState<backend::Cve2026_43499Policy>);
    static_assert(BackendState<backend::Cve43499_W1W2>);
    static_assert(!BackendState<backend::Cve2026_64560Policy>);
    static_assert(!BackendState<backend::Cve2026_31431Policy>);
    static_assert(!BackendState<backend::Cve2026_43503Policy>);
    static_assert(!BackendState<backend::Cve2026_23274Policy>);
    static_assert(BackendState<backend::Cve2026_43284Policy>);

    static_assert(backend::Cve2026_43499Policy::kind ==
                  contract::backend::Cve2026_43499::kind);
    static_assert(backend::Cve2026_64560Policy::kind ==
                  contract::backend::Cve2026_64560::kind);
    static_assert(backend::Cve2026_43284Policy::kind ==
                  contract::backend::Cve2026_43284::kind);

    /* Pipeline is the composition entry: one (backend, terminal) pair. */
    using RootChildPipeline = pipeline::Pipeline<backend::Cve2026_43499Policy,
                                                terminal::RootChildPolicy>;
    static_assert(RootChildPipeline::target == pipeline::DispatchTarget::Cve43499W1W3_RootChild);

    /* T4: the second catalogued step set is a distinct compile-time instance. */
    static_assert(BackendExecution<backend::Cve43499_W1W2, terminal::RootedChild>);
    static_assert(backend::Cve43499_W1W3::steps == contract::StepSetKind::W1W3);
    static_assert(backend::Cve43499_W1W2::steps == contract::StepSetKind::W1W2);
    using W1W2Pipeline = pipeline::Pipeline<backend::Cve43499_W1W2,
                                            terminal::RootChildPolicy>;
    static_assert(W1W2Pipeline::target == pipeline::DispatchTarget::Cve43499W1W2_RootChild);

    static_assert(contract::TerminalIdentity<terminal::RootChildPolicy>);
    static_assert(contract::TerminalExecution<terminal::RootChildPolicy>);
    static_assert(contract::TerminalIdentity<terminal::UmhForwardPolicy>);
    /* B5-8 landed the umh_forward step; B6/T5 wired its production probe and the
     * app-call device gate passed, so it is now available. */
    static_assert(contract::TerminalExecution<terminal::UmhForwardPolicy>);
    static_assert(contract::terminal_available(terminal::UmhForwardPolicy::kind));

    /* B5-8: the 43284 + PageCacheWrite + UmhForward triple is a catalogued,
     * compilable Pipeline instance; B6/T5 made it runnable on the production
     * path (execution_binding + readiness terminal) and the app-call device gate
     * passed. The target static_assert still proves the dispatch table entry. */
    using UmhPipeline = pipeline::Pipeline<backend::Cve2026_43284Policy,
                                           terminal::UmhForwardPolicy>;
    static_assert(UmhPipeline::backend == contract::BackendKind::Cve2026_43284);
    static_assert(UmhPipeline::steps == contract::StepSetKind::PageCacheWrite);
    static_assert(UmhPipeline::terminal == contract::TerminalKind::UmhForward);
    static_assert(UmhPipeline::target ==
                  pipeline::DispatchTarget::Cve43284PageCache_UmhForward);
    static_assert(contract::backend_available(contract::BackendKind::Cve2026_43284));

    /* Unified interface (R19/R20): every terminal declares Input + activation. */
    static_assert(std::is_same_v<terminal::RootChildPolicy::Input, terminal::RootedChild>);
    static_assert(std::is_base_of_v<terminal::TerminalInput, terminal::RootChildPolicy::Input>);
    static_assert(terminal::RootChildPolicy::activation == terminal::ActivationContext::Descendant);
    static_assert(std::is_same_v<terminal::UmhForwardPolicy::Input, terminal::UmhForwardInput>);
    static_assert(std::is_base_of_v<terminal::TerminalInput,
                                    terminal::UmhForwardPolicy::Input>);
    static_assert(terminal::UmhForwardPolicy::activation ==
                  terminal::ActivationContext::KernelSpawned);

    int32_t known = 0;
    contract::for_each_backend([&]<class B>() {
        known++;
        assert(pipeline::backend_name(B::kind) != "");
    });
    assert(known == 6);
    int32_t terminals = 0;
    contract::for_each_terminal([&]<class F>() {
        terminals++;
        assert(pipeline::terminal_name(F::kind) != "");
    });
    assert(terminals == 2);
    assert(contract::backend_available(contract::backend::Cve2026_43499::kind));
    assert(!contract::backend_available(contract::backend::Cve2026_64560::kind));

    puts("backend_contract_test: ok");
    return 0;
}
