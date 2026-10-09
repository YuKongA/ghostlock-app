#ifndef GHOSTLOCK_COMPONENT_CATALOG_HPP
#define GHOSTLOCK_COMPONENT_CATALOG_HPP

/* ADR-0004 + ADR-0006 T5 / S4 R6b composition catalogue.
 *
 * The composition authority is now (backend, token): the token lives in
 * backend.<id>.steps and already contains the route + terminal/path choice.
 * The sparse dispatch targets below are the path-level pipeline instantiations
 * (three wired paths); every wired token maps onto exactly one of them and
 * every Pipeline asserts its target at compile time. The token vocabulary
 * itself lives in contract/identity.hpp (kCombinationCatalog); this header only
 * composes it. Route stays backend-internal for the run itself (the token
 * selects it explicitly and the document middleware carries it). */

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#include "contract/chain_catalog.hpp"
#include "contract/kind.hpp"
#include "profile/route_catalog.hpp"

namespace ghostlock::pipeline {
    /* Route is backend-internal (ADR-0004 R2/R12): it reuses the profile route
     * enum and never appears in the selection. None means the backend has no
     * route axis; Auto is the deprecated legacy wire value 0. */
    using MiddlewareKind = ghostlock::profile::RouteId;

    [[nodiscard]] constexpr bool middleware_available(MiddlewareKind kind) noexcept {
        return kind == MiddlewareKind::TcpZerocopy ||
               kind == MiddlewareKind::SelectStack ||
               kind == MiddlewareKind::MulticastWaiter;
    }

    /* THE dispatch authority. The path-level target is the compile-time
     * Pipeline instantiation; dispatch_target_of(token) maps every wired token
     * onto it. A planned token (available=false) has no target. */
    enum class DispatchTarget : std::uint8_t {
        None,
        Cve43499W1W3_RootChild,
        Cve43499W1W2_RootChild,
        Cve43284PageCache_UmhForward,
    };

    /* Path-level target used by Pipeline<Backend, Terminal>; kept separate so the
     * pipeline asserts the path it is instantiated for while the catalogue
     * remains token-keyed. */


    /** Equality for the token strings the catalogues spell. A tiny local helper rather than a
     *  dependency: the tokens are NUL terminated pointers by construction, and comparing them
     *  through string_view keeps the check one expression wide. */
    [[nodiscard]] constexpr bool is_token(const char *spelled, std::string_view wanted) noexcept {
        if (spelled == nullptr) { return false; }
        return std::string_view(spelled) == wanted;
    }
    /** The dispatch target a chain row means, or None when it means nothing wired.
     *
     *  The row carries everything needed: the backend, the step set it runs, the path that says
     *  what happens to the child, the terminal, and whether this build wires it. So this
     *  function interprets contract::kChainCatalog and adds no vocabulary of its own -- an
     *  earlier attempt here introduced a combination type and a step-set and terminal enum to
     *  hold the same facts, which the wiring gate then exposed as duplication.
     *
     *  The row is a parameter rather than a lookup by position so that a test can hand in a row
     *  of its own: against the production table alone the unwired branch may be unreachable,
     *  and a guard that cannot fail is not a guard.
     *
     *  @param row  A chain row, from the catalogue or from a test.
     *  @return     The target this build dispatches that row to; None for anything unwired or
     *              not one of the wired paths.
     *  @note       The comparison is on tokens the contract layer spells, so a rename there is a
     *              compile-time change of string literals here and never a silent drift. */
    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        const contract::ChainSpec &row) noexcept {
        if (!row.wired) { return DispatchTarget::None; }
        if (is_token(row.backend, "cve_2026_43284") && is_token(row.step_set, "pagecache") &&
            is_token(row.terminal, "umh_forward")) {
            return DispatchTarget::Cve43284PageCache_UmhForward;
        }
        if (is_token(row.backend, "cve_2026_43499") && is_token(row.terminal, "root_child")) {
            if (is_token(row.step_set, "selinux_cred_seccomp")) {
                return DispatchTarget::Cve43499W1W3_RootChild;
            }
            if (is_token(row.step_set, "selinux_cred")) {
                return DispatchTarget::Cve43499W1W2_RootChild;
            }
        }
        return DispatchTarget::None;
    }

    /** Whether the catalogue has any wired row for this backend token. */
    [[nodiscard]] constexpr bool backend_is_wired(std::string_view backend_token) noexcept {
        for (std::size_t i = 0U; i < contract::kChainCount; ++i) {
            if (is_token(contract::kChainCatalog[i].backend, backend_token) &&
                contract::kChainCatalog[i].wired) {
                return true;
            }
        }
        return false;
    }
} // namespace ghostlock::pipeline

#endif
