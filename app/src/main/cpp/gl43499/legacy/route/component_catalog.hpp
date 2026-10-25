#ifndef GHOSTLOCK_COMPONENT_CATALOG_HPP
#define GHOSTLOCK_COMPONENT_CATALOG_HPP

#include <cstdint>
#include <string_view>

#include "profile/model.h"

namespace ghostlock::runtime {
    /* Stable component ids. Explicit numeric values; never rely on the
     * compiler's enum layout. Batch 3 registers one implementation per class
     * (root_child frontend, cve_2026_43499 backend); the UMH / 64560 ids are
     * reserved and report unavailable until their own batches land. */
    enum class FrontendKind : std::uint8_t {
        RootChild = 1,
        UmhForward = 2,
    };

    enum class BackendKind : std::uint8_t {
        Cve2026_43499 = 1,
        Cve2026_64560 = 2,
    };

    /* Middleware reuses the profile route enum: the three tactics are exactly
     * the set the wire carries. Auto means "unresolved" and is never
     * selectable. */
    using MiddlewareKind = ghostlock::profile::RouteKind;

    struct ComponentSelection final {
        FrontendKind frontend;
        BackendKind backend;
        MiddlewareKind middleware;
    };

    [[nodiscard]] constexpr bool frontend_available(FrontendKind kind) noexcept {
        return kind == FrontendKind::RootChild;
    }

    [[nodiscard]] constexpr bool backend_available(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499;
    }

    [[nodiscard]] constexpr bool middleware_available(MiddlewareKind kind) noexcept {
        return kind == MiddlewareKind::TcpZerocopy ||
               kind == MiddlewareKind::SelectStack ||
               kind == MiddlewareKind::MulticastWaiter;
    }

    /* Per-id availability pre-check: the three ids are each selectable. This
     * says nothing about whether the tuple is a catalogued pipeline. */
    [[nodiscard]] constexpr bool selection_supported(
        const ComponentSelection &selection) noexcept {
        return frontend_available(selection.frontend) &&
               backend_available(selection.backend) &&
               middleware_available(selection.middleware);
    }

    /* THE dispatch authority: the exact tuples the orchestrator can enumerate
     * and run. selection_supported() must hold for every supported combination,
     * but not every available triple is catalogued (e.g. Auto is never a
     * middleware, and future frontends/backends are added here explicitly).
     * Adding a component updates this catalogue and the orchestrator switch
     * together; the host test asserts the two never diverge. */
    [[nodiscard]] constexpr bool combination_supported(
        const ComponentSelection &selection) noexcept {
        return selection.frontend == FrontendKind::RootChild &&
               selection.backend == BackendKind::Cve2026_43499 &&
               middleware_available(selection.middleware);
    }

    /* Dispatch target for one catalogued combination. It names the FULL triple,
     * not just the middleware: opening a second frontend/backend must extend
     * this enum and the mapping below, so the dispatch can never key on a
     * single axis while the catalogue grows on the others. Pipeline exposes it
     * as Pipeline::target and every orchestrator case asserts its own target
     * against that value, so a branch cannot be wired to another (still
     * supported) combination without failing to compile. */
    enum class DispatchTarget : std::uint8_t {
        None,
        RootChild_Cve43499_SelectStack,
        RootChild_Cve43499_TcpZerocopy,
        RootChild_Cve43499_MulticastWaiter,
    };

    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        FrontendKind frontend, BackendKind backend,
        MiddlewareKind middleware) noexcept {
        if (frontend != FrontendKind::RootChild ||
            backend != BackendKind::Cve2026_43499) {
            return DispatchTarget::None;
        }
        switch (middleware) {
            case MiddlewareKind::SelectStack:
                return DispatchTarget::RootChild_Cve43499_SelectStack;
            case MiddlewareKind::TcpZerocopy:
                return DispatchTarget::RootChild_Cve43499_TcpZerocopy;
            case MiddlewareKind::MulticastWaiter:
                return DispatchTarget::RootChild_Cve43499_MulticastWaiter;
            default:
                return DispatchTarget::None;
        }
    }

    [[nodiscard]] constexpr DispatchTarget dispatch_target(
        const ComponentSelection &selection) noexcept {
        if (!combination_supported(selection)) return DispatchTarget::None;
        return dispatch_target_of(selection.frontend, selection.backend,
                                  selection.middleware);
    }

    [[nodiscard]] constexpr std::string_view frontend_name(FrontendKind kind) noexcept {
        return kind == FrontendKind::RootChild ? "root_child" : "umh_forward";
    }

    [[nodiscard]] constexpr std::string_view backend_name(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499 ? "cve_2026_43499" : "cve_2026_64560";
    }

    [[nodiscard]] constexpr std::string_view middleware_name(MiddlewareKind kind) noexcept {
        switch (kind) {
            case MiddlewareKind::TcpZerocopy: return "tcp_zerocopy";
            case MiddlewareKind::SelectStack: return "select_stack";
            case MiddlewareKind::MulticastWaiter: return "multicast_waiter";
            default: return "auto";
        }
    }
} // namespace ghostlock::runtime

#endif
