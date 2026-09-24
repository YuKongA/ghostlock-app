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

    /* Single compatibility check the orchestrator runs before dispatch. */
    [[nodiscard]] constexpr bool selection_supported(
        const ComponentSelection &selection) noexcept {
        return frontend_available(selection.frontend) &&
               backend_available(selection.backend) &&
               middleware_available(selection.middleware);
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
