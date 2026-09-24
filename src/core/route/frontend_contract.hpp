#ifndef GHOSTLOCK_FRONTEND_CONTRACT_HPP
#define GHOSTLOCK_FRONTEND_CONTRACT_HPP

#include <cstdint>
#include <string_view>

#include "route/component_catalog.hpp"

namespace ghostlock::runtime::frontend {
    /* Batch 4 contract scaffolding. It declares only the compile-time id,
     * availability and failure reason; there is no provider operation here and
     * no execution path. The root_child frontend is realized by the existing
     * ExploitProcedure / victim / handoff code and is NOT split out by this
     * batch (see the Batch 4 design "实现边界").
     *
     * Responsibility split (do not merge):
     *   - child lifecycle boundary: session/victim_context.* + victim_process.*
     *     (pipe ends, child pid, retire/release);
     *   - root handoff / KernelSU manager verification: session/handoff_probe.*
     *     (module visibility, ksu log, enforce poll).
     * A future UMH frontend must not be bound to KernelSU. */

    struct RootChildFrontend final {
        static constexpr FrontendKind kind = FrontendKind::RootChild;
        static constexpr bool available = true;
        static constexpr std::string_view unavailable_reason = "";
    };

    struct UmhForwardFrontend final {
        static constexpr FrontendKind kind = FrontendKind::UmhForward;
        static constexpr bool available = false;
        static constexpr std::string_view unavailable_reason =
            "umh_forward frontend is not implemented";
    };

    [[nodiscard]] constexpr bool available(FrontendKind kind) noexcept {
        return kind == FrontendKind::RootChild;
    }
} // namespace ghostlock::runtime::frontend

#endif
