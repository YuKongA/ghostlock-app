#ifndef GHOSTLOCK_FRONTEND_CONTRACT_HPP
#define GHOSTLOCK_FRONTEND_CONTRACT_HPP

#include <cstdint>
#include <string_view>

#include "route/component_catalog.hpp"

namespace ghostlock::runtime::frontend {
    /* Batch 4 DECLARATION-ONLY scaffolding. These structs name the known
     * frontend ids and their failure reason; they are NOT wired into selection
     * and expose no provider operation or execution path. Availability is owned
     * by component_catalog::frontend_available(), and the static_asserts below
     * fail to compile if this declaration ever drifts from it.
     *
     * The root_child frontend is realized by session/root_child_frontend.*
     * (handoff step) with the victim/handoff code below.
     *
     * Responsibility split (do not merge):
     *   - child lifecycle boundary: session/victim_context.* + victim_process.*
     *     (pipe ends, child pid, retire/release);
     *   - root handoff / KernelSU manager verification: session/handoff_probe.*
     *     (module visibility, ksu log, enforce poll).
     * A future UMH frontend must not be bound to KernelSU. */

    struct RootChildFrontend final {
        static constexpr FrontendKind kind = FrontendKind::RootChild;
        static constexpr std::string_view unavailable_reason = "";
    };

    struct UmhForwardFrontend final {
        static constexpr FrontendKind kind = FrontendKind::UmhForward;
        static constexpr std::string_view unavailable_reason =
            "umh_forward frontend is not implemented";
    };

    /* Declarations must match the catalog authority. */
    static_assert(frontend_available(RootChildFrontend::kind));
    static_assert(!frontend_available(UmhForwardFrontend::kind));
} // namespace ghostlock::runtime::frontend

#endif
