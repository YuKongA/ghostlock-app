/*
 * GhostLock — middleware route entry (Batch 4, D1=B slice 3b).
 *
 * Single route write, moved out of the retired procedure; behavior is
 * unchanged (the route policy and race still run inside run_main_route_threads).
 */

#include "backend/cve_2026_43499/route/route_middleware.hpp"

#include "race/threads.hpp"

namespace ghostlock::backend::cve_2026_43499::route::middleware {
    Status run_middleware_route(session::CoreSession &session,
                                const memory::WriteRequest &request) {
        (void) session;
        return race::run_main_route_threads(request);
    }
} // namespace ghostlock::backend::cve_2026_43499::route::middleware
