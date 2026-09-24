#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "route/component_catalog.hpp"

namespace ghostlock::runtime {
    /* Batch 4 (D1=B) pipeline composition shape. It pins the three component
     * kinds of one selected pipeline at compile time; availability stays owned
     * by component_catalog (no second availability definition here). Only the
     * frontend step is actually split out so far (root_child handoff); the
     * backend (W1-W3) and middleware (route/race) implementations still live in
     * ExploitProcedure and are not yet wired through this type. */
    template <class Frontend, class Backend, class Middleware>
    struct Pipeline final {
        static constexpr FrontendKind frontend = Frontend::kind;
        static constexpr BackendKind backend = Backend::kind;
        static constexpr MiddlewareKind middleware = Middleware::kind;
    };

    /* A pipeline is dispatchable only when all three kinds are available. */
    template <class Frontend, class Backend, class Middleware>
    [[nodiscard]] constexpr bool pipeline_supported() noexcept {
        return frontend_available(Frontend::kind) &&
               backend_available(Backend::kind) &&
               middleware_available(Middleware::kind);
    }
} // namespace ghostlock::runtime

#endif
