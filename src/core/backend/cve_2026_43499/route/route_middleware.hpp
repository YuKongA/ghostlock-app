#ifndef GHOSTLOCK_ROUTE_MIDDLEWARE_HPP
#define GHOSTLOCK_ROUTE_MIDDLEWARE_HPP

#include "memory/payload_builder.h"
#include "session/core_session.hpp"
#include "support/status.hpp"

namespace ghostlock::backend::cve_2026_43499::route::middleware {
    /* Batch 4 (D1=B slice 3b) middleware entry for one route write; called from
     * the cve_2026_43499 backend's attack_write. The route/race behavior is
     * unchanged (run_main_route_threads still selects and runs the route
     * policy). */
    Status run_middleware_route(session::CoreSession &session,
                                const memory::WriteRequest &request);
} // namespace ghostlock::backend::cve_2026_43499::route::middleware

#endif
