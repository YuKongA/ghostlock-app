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
#include <string_view>

#include "contract/identity.hpp"
#include "contract/model.hpp"

namespace ghostlock::pipeline {
    /* Route is backend-internal (ADR-0004 R2/R12): it reuses the profile route
     * enum and never appears in the selection. None means the backend has no
     * route axis; Auto is the deprecated legacy wire value 0. */
    using MiddlewareKind = ghostlock::profile::RouteKind;

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
    [[nodiscard]] constexpr DispatchTarget path_target_of(
        contract::BackendKind backend, contract::StepSetKind steps,
        contract::TerminalKind terminal) noexcept {
        if (backend == contract::BackendKind::Cve2026_43499 &&
            steps == contract::StepSetKind::W1W3 &&
            terminal == contract::TerminalKind::RootChild) {
            return DispatchTarget::Cve43499W1W3_RootChild;
        }
        if (backend == contract::BackendKind::Cve2026_43499 &&
            steps == contract::StepSetKind::W1W2 &&
            terminal == contract::TerminalKind::RootChild) {
            return DispatchTarget::Cve43499W1W2_RootChild;
        }
        if (backend == contract::BackendKind::Cve2026_43284 &&
            steps == contract::StepSetKind::PageCacheWrite &&
            terminal == contract::TerminalKind::UmhForward) {
            return DispatchTarget::Cve43284PageCache_UmhForward;
        }
        return DispatchTarget::None;
    }

    /* Token -> path-level dispatch target. Only wired (available) tokens have a
     * target; a planned token maps to None. */
    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        contract::BackendKind backend, contract::CombinationKind combination) noexcept {
        const contract::CombinationSpec *spec = contract::combination_spec(combination);
        if (spec == nullptr || spec->backend != backend || !spec->available) {
            return DispatchTarget::None;
        }
        return path_target_of(backend, spec->steps, spec->terminal);
    }

    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        contract::CombinationKind combination) noexcept {
        const contract::CombinationSpec *spec = contract::combination_spec(combination);
        return spec != nullptr ? dispatch_target_of(spec->backend, combination)
                               : DispatchTarget::None;
    }

    /* Path-level compatibility alias (route is not part of the decomposed
     * triple). Prefer the token-keyed overload above. */
    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        contract::BackendKind backend, contract::StepSetKind steps,
        contract::TerminalKind terminal) noexcept {
        return path_target_of(backend, steps, terminal);
    }

    /* Token-keyed wiring predicate. combination_supported() says the token is
     * wired (has a Pipeline and a dispatch case); it is independent of device
     * availability, which stays in contract::selection_supported(). */
    [[nodiscard]] constexpr bool combination_supported(
        contract::BackendKind backend, contract::CombinationKind combination) noexcept {
        return dispatch_target_of(backend, combination) != DispatchTarget::None;
    }

    /* Compatibility predicate for callers that still hold the decomposed
     * (backend, steps, terminal) selection (main.cpp, legacy tests): true when
     * SOME wired token decomposes to that triple. Route is not part of
     * ComponentSelection, so this cannot be route-exact; the authoritative
     * checks are the token-keyed ones above. */
    [[nodiscard]] constexpr bool combination_supported(
        const contract::ComponentSelection &selection) noexcept {
        for (const contract::CombinationSpec &spec : contract::kCombinationCatalog) {
            if (spec.backend == selection.backend && spec.steps == selection.steps &&
                spec.terminal == selection.terminal && spec.available) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr DispatchTarget dispatch_target(
        const contract::ComponentSelection &selection) noexcept {
        if (!combination_supported(selection)) return DispatchTarget::None;
        return path_target_of(selection.backend, selection.steps, selection.terminal);
    }

    [[nodiscard]] constexpr contract::CombinationKind combination_from_token(
        contract::BackendKind backend, std::string_view token) noexcept {
        contract::CombinationKind out = contract::CombinationKind::Unknown;
        if (!contract::combination_resolve(backend, token, out)) {
            return contract::CombinationKind::Unknown;
        }
        return out;
    }

    [[nodiscard]] constexpr std::string_view combination_name(
        contract::CombinationKind kind) noexcept {
        return contract::combination_name(kind);
    }

    [[nodiscard]] constexpr bool combination_available(
        contract::CombinationKind kind) noexcept {
        return contract::combination_available(kind);
    }

    [[nodiscard]] constexpr contract::TerminalKind combination_terminal(
        contract::CombinationKind kind) noexcept {
        const contract::CombinationSpec *spec = contract::combination_spec(kind);
        return spec != nullptr ? spec->terminal : contract::TerminalKind::RootChild;
    }

    [[nodiscard]] constexpr MiddlewareKind combination_route(
        contract::CombinationKind kind) noexcept {
        const contract::CombinationSpec *spec = contract::combination_spec(kind);
        return spec != nullptr ? spec->route : MiddlewareKind::None;
    }

    [[nodiscard]] constexpr std::string_view terminal_name(
        contract::TerminalKind kind) noexcept {
        return contract::terminal_token_name(kind);
    }

    [[nodiscard]] constexpr std::string_view backend_name(
        contract::BackendKind kind) noexcept {
        return contract::backend_token_name(kind);
    }

    [[nodiscard]] constexpr std::string_view stepset_name(
        contract::StepSetKind kind) noexcept {
        switch (kind) {
            case contract::StepSetKind::W1W2: return "w1_w2";
            case contract::StepSetKind::W1W3: return "w1_w3";
            case contract::StepSetKind::PageCacheWrite: return "pagecache_write";
            default: return "unknown";
        }
    }

    /* Name of a route/middleware kind: the ONE spelling table lives in the
     * contract (contract::route_name) so the catalogue, the exported manifests
     * and this accessor cannot drift. RouteKind::None and the deprecated legacy
     * wire value 0 (no enumerator in the current vocabulary) both report as
     * "none". */
    [[nodiscard]] constexpr std::string_view middleware_name(MiddlewareKind kind) noexcept {
        return contract::route_name(kind);
    }

    /* Wire-token resolution. The GLKv3 root names the terminal/backend as text;
     * the composition root owns the vocabulary and maps it here. */
    [[nodiscard]] constexpr bool terminal_from_token(std::string_view token,
                                                     contract::TerminalKind &out) noexcept {
        for (const contract::TerminalKind kind : {contract::TerminalKind::RootChild,
                                                  contract::TerminalKind::UmhForward}) {
            if (token == terminal_name(kind)) {
                out = kind;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool backend_from_token(std::string_view token,
                                                    contract::BackendKind &out) noexcept {
        return contract::backend_kind_from_token(token, out);
    }
} // namespace ghostlock::pipeline

#endif
