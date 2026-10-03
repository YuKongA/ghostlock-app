/* Host test for the backend/terminal contracts: identity vs execution, the
 * single availability authority, and the declared registry against the
 * catalogue. */

#include "pipeline/backend_contract.hpp"
#include "pipeline/pipeline.hpp"
#include "backend/cve_2026_23274_backend.hpp"
#include "backend/cve_2026_43284_backend.hpp"
#include "backend/cve_2026_31431_backend.hpp"
#include "backend/cve_2026_43499_backend.hpp"
#include "backend/cve_2026_43503_backend.hpp"
#include "backend/cve_2026_64560_backend.hpp"
#include "terminal/root_child.hpp"
#include "terminal/terminal_input.hpp"

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
        static constexpr pipeline::BackendKind kind = pipeline::BackendKind::Cve2026_43284;

        [[nodiscard]] static session::StageResult run(
            session::CoreSession &, const profile::kernel_offsets &, const char *,
            bool, terminal::UmhForwardInput &);
    };
} // namespace

int32_t main(void) {
    using pipeline::BackendExecution;
    using pipeline::BackendIdentity;

    static_assert(BackendIdentity<pipeline::backend::Cve2026_43499>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_64560>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_31431>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_43503>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_23274>);
    static_assert(BackendIdentity<pipeline::backend::Cve2026_43284>);

    /* The available backend provides the step entry; the placeholders do not. */
    static_assert(BackendExecution<backend::Cve2026_43499Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_64560Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_31431Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_43503Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_23274Policy, terminal::RootedChild>);
    static_assert(!BackendExecution<backend::Cve2026_43284Policy, terminal::RootedChild>);

    /* R10 continuation: BackendExecution is parameterised on the terminal input
     * type. cve_2026_43499 only fills RootedChild, so it does not satisfy the
     * contract for UmhForwardInput; the test double proves the converse. */
    static_assert(!BackendExecution<backend::Cve2026_43499Policy, terminal::UmhForwardInput>);
    static_assert(BackendExecution<UmhInputPlaceholderBackend, terminal::UmhForwardInput>);
    static_assert(!BackendExecution<UmhInputPlaceholderBackend, terminal::RootedChild>);

    /* B2: the available backend provides its state contract; the placeholders,
     * which never reach Pipeline, carry none. */
    using pipeline::BackendState;
    static_assert(BackendState<backend::Cve2026_43499Policy>);
    static_assert(BackendState<backend::Cve43499_W1W2>);
    static_assert(!BackendState<backend::Cve2026_64560Policy>);
    static_assert(!BackendState<backend::Cve2026_31431Policy>);
    static_assert(!BackendState<backend::Cve2026_43503Policy>);
    static_assert(!BackendState<backend::Cve2026_23274Policy>);
    static_assert(!BackendState<backend::Cve2026_43284Policy>);

    static_assert(backend::Cve2026_43499Policy::kind ==
                  pipeline::backend::Cve2026_43499::kind);
    static_assert(backend::Cve2026_64560Policy::kind ==
                  pipeline::backend::Cve2026_64560::kind);
    static_assert(backend::Cve2026_43284Policy::kind ==
                  pipeline::backend::Cve2026_43284::kind);

    /* Pipeline is the composition entry: one (backend, terminal) pair. */
    using RootChildPipeline = pipeline::Pipeline<backend::Cve2026_43499Policy,
                                                terminal::RootChildPolicy>;
    static_assert(RootChildPipeline::target == pipeline::DispatchTarget::Cve43499W1W3_RootChild);

    /* T4: the second catalogued step set is a distinct compile-time instance. */
    static_assert(BackendExecution<backend::Cve43499_W1W2, terminal::RootedChild>);
    static_assert(backend::Cve43499_W1W3::steps == pipeline::StepSetKind::W1W3);
    static_assert(backend::Cve43499_W1W2::steps == pipeline::StepSetKind::W1W2);
    using W1W2Pipeline = pipeline::Pipeline<backend::Cve43499_W1W2,
                                            terminal::RootChildPolicy>;
    static_assert(W1W2Pipeline::target == pipeline::DispatchTarget::Cve43499W1W2_RootChild);

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
    assert(known == 6);
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
