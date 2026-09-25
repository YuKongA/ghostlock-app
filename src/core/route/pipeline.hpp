#ifndef GHOSTLOCK_PIPELINE_HPP
#define GHOSTLOCK_PIPELINE_HPP

#include "route/component_catalog.hpp"
#include "session/exploit_session.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::runtime {
    /* Batch 4 (D1=B) pipeline composition shape. It pins the three component
     * kinds of one selected pipeline at compile time; availability stays owned
     * by component_catalog (no second availability definition here). */
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

    /* Run one catalogued pipeline. The backend policy owns the write-stage
     * sequence (Continue hands the filled chain to the frontend), the frontend
     * policy owns the handoff step, and the middleware route hooks are policy
     * static members called directly from the backend steps - no vtable or
     * indirect dispatch enters the path. */
    template <class Frontend, class Backend, class Middleware>
    [[nodiscard]] int32_t run_pipeline(session::ExploitSession &exploit_session,
                                       const profile::kernel_offsets &decoded,
                                       const char *debug_dir, bool force_attack) {
        static_assert(pipeline_supported<Frontend, Backend, Middleware>());
        session::VictimChain chain{};
        switch (Backend::run(exploit_session, decoded, debug_dir, force_attack, chain)) {
            case session::StageResult::Failed:
                return 1;
            case session::StageResult::Done:
                return 0;
            case session::StageResult::Continue:
                break;
        }
        return Frontend::run(exploit_session, chain) == session::StageResult::Failed ? 1 : 0;
    }
} // namespace ghostlock::runtime

#endif
