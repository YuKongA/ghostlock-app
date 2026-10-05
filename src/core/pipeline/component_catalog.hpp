#ifndef GHOSTLOCK_COMPONENT_CATALOG_HPP
#define GHOSTLOCK_COMPONENT_CATALOG_HPP

/* ADR-0004 composition catalogue: the sparse (backend, steps, terminal) triple
 * table and its dispatch targets, over the identity vocabulary declared in
 * contract/identity.hpp. Identity, availability and the execution contracts
 * live in contract; this header keeps only composition. Route is backend-
 * internal, so it is chosen by the backend from the profile and is not part of
 * the catalogue. */

#include <cstdint>
#include <string_view>

#include "contract/identity.hpp"
#include "contract/model.hpp"

namespace ghostlock::pipeline {
    /* Route is backend-internal (ADR-0004 R2/R12): it reuses the profile route
     * enum and never appears in the selection. Auto means "unresolved". */
    using MiddlewareKind = ghostlock::profile::RouteKind;

    [[nodiscard]] constexpr bool middleware_available(MiddlewareKind kind) noexcept {
        return kind == MiddlewareKind::TcpZerocopy ||
               kind == MiddlewareKind::SelectStack ||
               kind == MiddlewareKind::MulticastWaiter;
    }

    /* THE dispatch authority: the exact (backend, steps, terminal) triples wired
     * into DispatchTarget and the orchestrator switch. This is the *catalogue*:
     * a triple is listed once it has a compile-time Pipeline instantiation and an
     * orchestrator case, independent of whether its backend is device-verified.
     * The orchestrator additionally requires contract::selection_supported(), so
     * wiring a triple here does not make it runnable -- the two predicates answer
     * different questions (wired vs. verified). Route is chosen by the backend
     * from the profile, so it is not part of this catalogue. Adding a component
     * updates this catalogue and the orchestrator switch together; the host test
     * asserts the two never diverge. This is a sparse enumeration, never a dense
     * product (ADR-0004 R21). */
    [[nodiscard]] constexpr bool combination_supported(
        const contract::ComponentSelection &selection) noexcept {
        if (selection.backend == contract::BackendKind::Cve2026_43499 &&
            (selection.steps == contract::StepSetKind::W1W3 ||
             selection.steps == contract::StepSetKind::W1W2) &&
            selection.terminal == contract::TerminalKind::RootChild) {
            return true;
        }
        return selection.backend == contract::BackendKind::Cve2026_43284 &&
               selection.steps == contract::StepSetKind::PageCacheWrite &&
               selection.terminal == contract::TerminalKind::UmhForward;
    }

    /* Dispatch target for one catalogued triple. Pipeline exposes it as
     * Pipeline::target and every orchestrator case asserts against that value,
     * so a branch cannot be wired to another supported triple. */
    enum class DispatchTarget : std::uint8_t {
        None,
        Cve43499W1W3_RootChild,
        Cve43499W1W2_RootChild,
        /* B5-8: wired for compile-time/orchestrator coverage; the backend stays
         * unavailable (selection_supported false) until the B5-9 device gate. */
        Cve43284PageCache_UmhForward,
    };

    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
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

    [[nodiscard]] constexpr DispatchTarget dispatch_target(
        const contract::ComponentSelection &selection) noexcept {
        if (!combination_supported(selection)) return DispatchTarget::None;
        return dispatch_target_of(selection.backend, selection.steps, selection.terminal);
    }

    [[nodiscard]] constexpr std::string_view terminal_name(
        contract::TerminalKind kind) noexcept {
        return kind == contract::TerminalKind::RootChild ? "root_child" : "umh_forward";
    }

    [[nodiscard]] constexpr std::string_view backend_name(
        contract::BackendKind kind) noexcept {
        switch (kind) {
            case contract::BackendKind::Cve2026_43499: return "cve_2026_43499";
            case contract::BackendKind::Cve2026_64560: return "cve_2026_64560";
            case contract::BackendKind::Cve2026_31431: return "cve_2026_31431";
            case contract::BackendKind::Cve2026_43503: return "cve_2026_43503";
            case contract::BackendKind::Cve2026_23274: return "cve_2026_23274";
            case contract::BackendKind::Cve2026_43284: return "cve_2026_43284";
            default: return "unknown";
        }
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

    [[nodiscard]] constexpr std::string_view middleware_name(MiddlewareKind kind) noexcept {
        switch (kind) {
            case MiddlewareKind::TcpZerocopy: return "tcp_zerocopy";
            case MiddlewareKind::SelectStack: return "select_stack";
            case MiddlewareKind::MulticastWaiter: return "multicast_waiter";
            default: return "auto";
        }
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
        for (const contract::BackendKind kind :
             {contract::BackendKind::Cve2026_43499, contract::BackendKind::Cve2026_64560,
              contract::BackendKind::Cve2026_31431, contract::BackendKind::Cve2026_43503,
              contract::BackendKind::Cve2026_23274, contract::BackendKind::Cve2026_43284}) {
            if (token == backend_name(kind)) {
                out = kind;
                return true;
            }
        }
        return false;
    }
} // namespace ghostlock::pipeline

#endif
