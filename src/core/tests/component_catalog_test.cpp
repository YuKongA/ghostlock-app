/* Host test for the component catalog after ADR-0006 T5 / S4 R6b: the
 * composition authority is (backend, combination token). The token whitelist,
 * its availability flags, the token -> (route, steps, path) derivation and the
 * token -> dispatch target mapping are all asserted here; unknown tokens and
 * planned (available=false) tokens must never dispatch. */

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string_view>

using namespace ghostlock;

namespace {
    using contract::BackendKind;
    using contract::CombinationKind;
    using contract::CombinationSpec;
    using contract::StepSetKind;
    using contract::TerminalKind;

    const CombinationSpec *spec_of(CombinationKind kind) {
        const CombinationSpec *spec = contract::combination_spec(kind);
        assert(spec != nullptr);
        return spec;
    }

    bool token_is_well_formed(std::string_view token, BackendKind backend) {
        if (backend == BackendKind::Cve2026_43284) {
            return token == "umh" || token == "rootchild" || token == "shizuku";
        }
        const std::size_t underscore = token.find('_');
        if (underscore == std::string_view::npos) return false;
        const std::string_view prefix = token.substr(0, underscore);
        const std::string_view path = token.substr(underscore + 1);
        const bool prefix_ok =
                prefix == "mcast" || prefix == "pselect" || prefix == "tcp";
        const bool path_ok =
                path == "rootchild" || path == "shizuku" || path == "umh";
        return prefix_ok && path_ok;
    }
} // namespace

int32_t main(void) {
    assert(contract::terminal_available(TerminalKind::RootChild));
    assert(contract::terminal_available(TerminalKind::UmhForward));
    assert(contract::backend_available(BackendKind::Cve2026_43499));
    assert(!contract::backend_available(BackendKind::Cve2026_64560));
    assert(!contract::backend_available(BackendKind::Cve2026_31431));
    assert(!contract::backend_available(BackendKind::Cve2026_43503));
    assert(!contract::backend_available(BackendKind::Cve2026_23274));
    assert(contract::backend_available(BackendKind::Cve2026_43284));
    for (StepSetKind s : {StepSetKind::W1W2, StepSetKind::W1W3, StepSetKind::PageCacheWrite}) {
        assert(contract::stepset_available(s));
    }
    for (pipeline::MiddlewareKind kind : {pipeline::MiddlewareKind::TcpZerocopy,
                                          pipeline::MiddlewareKind::SelectStack,
                                          pipeline::MiddlewareKind::MulticastWaiter}) {
        assert(pipeline::middleware_available(kind));
    }
    assert(!pipeline::middleware_available(pipeline::MiddlewareKind::None));

    /* ---- Token whitelist: 12 tokens, 7 wired, 5 planned. ---- */
    std::size_t total = 0;
    std::size_t wired = 0;
    std::size_t planned = 0;
    for (const CombinationSpec &spec : contract::kCombinationCatalog) {
        ++total;
        assert(spec.kind != CombinationKind::Unknown);
        assert(!spec.token.empty());
        assert(token_is_well_formed(spec.token, spec.backend));
        assert(contract::combination_name(spec.kind) == spec.token);
        assert(contract::combination_spec(spec.kind) == &spec);
        /* Token <-> (route, steps, path) derivation is self-consistent. */
        CombinationKind resolved = CombinationKind::Unknown;
        assert(contract::combination_resolve(spec.backend, spec.token, resolved));
        assert(resolved == spec.kind);
        if (spec.available) {
            ++wired;
            assert(contract::combination_available(spec.kind));
            assert(pipeline::dispatch_target_of(spec.kind) !=
                   pipeline::DispatchTarget::None);
        } else {
            ++planned;
            assert(!contract::combination_available(spec.kind));
            assert(pipeline::dispatch_target_of(spec.kind) ==
                   pipeline::DispatchTarget::None);
        }
    }
    assert(total == 12);
    assert(wired == 7);
    assert(planned == 5);

    /* ---- The exact token whitelist. ---- */
    const char *wired_tokens[] = {
        "mcast_rootchild", "pselect_rootchild", "tcp_rootchild",
        "mcast_shizuku",  "pselect_shizuku",  "tcp_shizuku",
        "umh",
    };
    for (const char *token : wired_tokens) {
        CombinationKind kind = CombinationKind::Unknown;
        const BackendKind backend = std::string_view(token) == "umh"
                                            ? BackendKind::Cve2026_43284
                                            : BackendKind::Cve2026_43499;
        assert(contract::combination_resolve(backend, token, kind));
        assert(contract::combination_available(kind));
    }
    const char *planned_tokens[] = {"mcast_umh", "pselect_umh", "tcp_umh",
                                    "rootchild", "shizuku"};
    for (const char *token : planned_tokens) {
        CombinationKind kind = CombinationKind::Unknown;
        assert(contract::combination_resolve(BackendKind::Cve2026_43499, token, kind) ||
               contract::combination_resolve(BackendKind::Cve2026_43284, token, kind));
        assert(kind != CombinationKind::Unknown);
        assert(!contract::combination_available(kind));
        assert(pipeline::dispatch_target_of(kind) == pipeline::DispatchTarget::None);
    }
    /* Unknown tokens are rejected with a null/Unknown resolution. */
    for (const char *token : {"mcast_umh_rootchild", "MCAST_ROOTCHILD", "w1_w3",
                              "pagecache_write", "root_child", ""}) {
        CombinationKind kind = CombinationKind::Unknown;
        assert(!contract::combination_resolve(BackendKind::Cve2026_43499, token, kind));
        assert(kind == CombinationKind::Unknown);
    }

    /* ---- Token -> terminal/step set derivation. ---- */
    assert(spec_of(CombinationKind::McastRootchild)->route ==
           profile::RouteKind::MulticastWaiter);
    assert(spec_of(CombinationKind::PselectRootchild)->route ==
           profile::RouteKind::SelectStack);
    assert(spec_of(CombinationKind::TcpRootchild)->route ==
           profile::RouteKind::TcpZerocopy);
    assert(spec_of(CombinationKind::McastRootchild)->steps == StepSetKind::W1W3);
    assert(spec_of(CombinationKind::McastShizuku)->steps == StepSetKind::W1W2);
    assert(spec_of(CombinationKind::McastRootchild)->terminal == TerminalKind::RootChild);
    assert(spec_of(CombinationKind::Umh)->terminal == TerminalKind::UmhForward);
    assert(spec_of(CombinationKind::Umh)->route == profile::RouteKind::None);
    assert(pipeline::combination_terminal(CombinationKind::TcpShizuku) ==
           TerminalKind::RootChild);
    assert(pipeline::combination_route(CombinationKind::PselectRootchild) ==
           pipeline::MiddlewareKind::SelectStack);
    assert(pipeline::combination_route(CombinationKind::Umh) ==
           pipeline::MiddlewareKind::None);

    /* ---- Dispatch targets are per wired path. ---- */
    assert(pipeline::dispatch_target_of(CombinationKind::McastRootchild) ==
           pipeline::DispatchTarget::Cve43499W1W3_RootChild);
    assert(pipeline::dispatch_target_of(CombinationKind::TcpShizuku) ==
           pipeline::DispatchTarget::Cve43499W1W2_RootChild);
    assert(pipeline::dispatch_target_of(CombinationKind::Umh) ==
           pipeline::DispatchTarget::Cve43284PageCache_UmhForward);
    assert(pipeline::combination_supported(BackendKind::Cve2026_43499,
                                           CombinationKind::McastRootchild));
    assert(!pipeline::combination_supported(BackendKind::Cve2026_43499,
                                            CombinationKind::Umh));
    assert(!pipeline::combination_supported(BackendKind::Cve2026_43284,
                                            CombinationKind::Rootchild));

    /* ---- Decomposed compatibility predicate (main.cpp / legacy tests). ---- */
    assert(pipeline::combination_supported({BackendKind::Cve2026_43499,
                                            StepSetKind::W1W3, TerminalKind::RootChild}));
    assert(pipeline::combination_supported({BackendKind::Cve2026_43499,
                                            StepSetKind::W1W2, TerminalKind::RootChild}));
    assert(pipeline::combination_supported({BackendKind::Cve2026_43284,
                                            StepSetKind::PageCacheWrite,
                                            TerminalKind::UmhForward}));
    assert(!pipeline::combination_supported({BackendKind::Cve2026_43499,
                                             StepSetKind::W1W3,
                                             TerminalKind::UmhForward}));
    assert(!pipeline::combination_supported({BackendKind::Cve2026_43284,
                                             StepSetKind::W1W3,
                                             TerminalKind::RootChild}));
    assert(!pipeline::combination_supported({BackendKind::Cve2026_64560,
                                             StepSetKind::W1W3,
                                             TerminalKind::RootChild}));
    assert(pipeline::dispatch_target({BackendKind::Cve2026_43499, StepSetKind::W1W3,
                                      TerminalKind::RootChild}) ==
           pipeline::DispatchTarget::Cve43499W1W3_RootChild);
    assert(pipeline::dispatch_target({BackendKind::Cve2026_43284,
                                      StepSetKind::PageCacheWrite,
                                      TerminalKind::UmhForward}) ==
           pipeline::DispatchTarget::Cve43284PageCache_UmhForward);

    /* ---- Names. ---- */
    assert(pipeline::terminal_name(TerminalKind::RootChild) == "root_child");
    assert(pipeline::terminal_name(TerminalKind::UmhForward) == "umh_forward");
    assert(pipeline::backend_name(BackendKind::Cve2026_43499) == "cve_2026_43499");
    assert(pipeline::backend_name(BackendKind::Cve2026_43284) == "cve_2026_43284");
    assert(pipeline::stepset_name(StepSetKind::W1W3) == "w1_w3");
    assert(pipeline::middleware_name(pipeline::MiddlewareKind::MulticastWaiter) ==
           "multicast_waiter");
    assert(pipeline::middleware_name(pipeline::MiddlewareKind::None) == "none");

    puts("component_catalog_test: ok (12 tokens, 7 wired, 5 planned)");
    return 0;
}
