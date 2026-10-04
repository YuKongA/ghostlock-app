#ifndef GHOSTLOCK_COMPONENT_CATALOG_HPP
#define GHOSTLOCK_COMPONENT_CATALOG_HPP

#include <cstdint>
#include <string_view>

#include "profile/model.h"

namespace ghostlock::pipeline {
    /* Stable component ids. Explicit numeric values; never rely on the
     * compiler's enum layout. The UMH terminal and the cve_2026_64560/31431/
     * 43503/23274/43284 backends are reserved and report unavailable until
     * their own batches land. */
    enum class TerminalKind : std::uint8_t {
        RootChild = 1,
        UmhForward = 2,
    };

    enum class BackendKind : std::uint8_t {
        Cve2026_43499 = 1,
        Cve2026_64560 = 2,
        Cve2026_31431 = 3,
        Cve2026_43503 = 4,
        Cve2026_23274 = 5,
        Cve2026_43284 = 6,
    };

    /* StepSet is backend-owned (ADR-0004 R18) but App-visible: the profile names
     * which step sequence the backend runs. W1W2 skips the seccomp bypass (shell
     * entry or kernel-spawned launch); W1W3 includes it (app-descendant launch).
     * PageCacheWrite is the cve_2026_43284 step vocabulary. */
    enum class StepSetKind : std::uint8_t {
        W1W2 = 1,
        W1W3 = 2,
        PageCacheWrite = 3,
    };

    /* Route is backend-internal (ADR-0004 R2/R12): it reuses the profile route
     * enum and never appears in the selection. Auto means "unresolved". */
    using MiddlewareKind = ghostlock::profile::RouteKind;

    struct ComponentSelection final {
        BackendKind backend;
        StepSetKind steps;
        TerminalKind terminal;
    };

    [[nodiscard]] constexpr bool terminal_available(TerminalKind kind) noexcept {
        return kind == TerminalKind::RootChild;
    }

    [[nodiscard]] constexpr bool backend_available(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499;
    }

    [[nodiscard]] constexpr bool stepset_available(StepSetKind kind) noexcept {
        return kind == StepSetKind::W1W2 || kind == StepSetKind::W1W3 ||
               kind == StepSetKind::PageCacheWrite;
    }

    [[nodiscard]] constexpr bool middleware_available(MiddlewareKind kind) noexcept {
        return kind == MiddlewareKind::TcpZerocopy ||
               kind == MiddlewareKind::SelectStack ||
               kind == MiddlewareKind::MulticastWaiter;
    }

    /* Per-axis availability pre-check: the runtime fail-closed gate, and the
     * only fact that says a selection may actually run on this build/device.
     * backend_available(Cve2026_43284) stays false until the B5-9 device gate
     * even though the 43284 triple is catalogued below; the composition root
     * checks this before dispatch, so catalogued-but-unverified never runs. */
    [[nodiscard]] constexpr bool selection_supported(
        const ComponentSelection &selection) noexcept {
        return backend_available(selection.backend) &&
               stepset_available(selection.steps) &&
               terminal_available(selection.terminal);
    }

    /* THE dispatch authority: the exact (backend, steps, terminal) triples wired
     * into DispatchTarget and the orchestrator switch. This is the *catalogue*:
     * a triple is listed once it has a compile-time Pipeline instantiation and an
     * orchestrator case, independent of whether its backend is device-verified.
     * The orchestrator additionally requires selection_supported(), so wiring a
     * triple here does not make it runnable -- the two predicates answer
     * different questions (wired vs. verified). Route is chosen by the backend
     * from the profile, so it is not part of this catalogue. Adding a component
     * updates this catalogue and the orchestrator switch together; the host test
     * asserts the two never diverge. This is a sparse enumeration, never a dense
     * product (ADR-0004 R21). */
    [[nodiscard]] constexpr bool combination_supported(
        const ComponentSelection &selection) noexcept {
        if (selection.backend == BackendKind::Cve2026_43499 &&
            (selection.steps == StepSetKind::W1W3 ||
             selection.steps == StepSetKind::W1W2) &&
            selection.terminal == TerminalKind::RootChild) {
            return true;
        }
        return selection.backend == BackendKind::Cve2026_43284 &&
               selection.steps == StepSetKind::PageCacheWrite &&
               selection.terminal == TerminalKind::UmhForward;
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
        BackendKind backend, StepSetKind steps, TerminalKind terminal) noexcept {
        if (backend == BackendKind::Cve2026_43499 && steps == StepSetKind::W1W3 &&
            terminal == TerminalKind::RootChild) {
            return DispatchTarget::Cve43499W1W3_RootChild;
        }
        if (backend == BackendKind::Cve2026_43499 && steps == StepSetKind::W1W2 &&
            terminal == TerminalKind::RootChild) {
            return DispatchTarget::Cve43499W1W2_RootChild;
        }
        if (backend == BackendKind::Cve2026_43284 &&
            steps == StepSetKind::PageCacheWrite &&
            terminal == TerminalKind::UmhForward) {
            return DispatchTarget::Cve43284PageCache_UmhForward;
        }
        return DispatchTarget::None;
    }

    [[nodiscard]] constexpr DispatchTarget dispatch_target(
        const ComponentSelection &selection) noexcept {
        if (!combination_supported(selection)) return DispatchTarget::None;
        return dispatch_target_of(selection.backend, selection.steps, selection.terminal);
    }

    [[nodiscard]] constexpr std::string_view terminal_name(TerminalKind kind) noexcept {
        return kind == TerminalKind::RootChild ? "root_child" : "umh_forward";
    }

    [[nodiscard]] constexpr std::string_view backend_name(BackendKind kind) noexcept {
        switch (kind) {
            case BackendKind::Cve2026_43499: return "cve_2026_43499";
            case BackendKind::Cve2026_64560: return "cve_2026_64560";
            case BackendKind::Cve2026_31431: return "cve_2026_31431";
            case BackendKind::Cve2026_43503: return "cve_2026_43503";
            case BackendKind::Cve2026_23274: return "cve_2026_23274";
            case BackendKind::Cve2026_43284: return "cve_2026_43284";
            default: return "unknown";
        }
    }

    [[nodiscard]] constexpr std::string_view stepset_name(StepSetKind kind) noexcept {
        switch (kind) {
            case StepSetKind::W1W2: return "w1_w2";
            case StepSetKind::W1W3: return "w1_w3";
            case StepSetKind::PageCacheWrite: return "pagecache_write";
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
} // namespace ghostlock::pipeline

#endif
