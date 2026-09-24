/*
 * GhostLock — middleware route entry (Batch 4, D1=B slice 3b).
 *
 * Single route write, moved out of ExploitProcedure::attack_write; behavior is
 * unchanged (the route policy and race still run inside run_main_route_threads).
 */

#include "route/route_middleware.hpp"

#include "race/threads.hpp"

namespace ghostlock::route::middleware {
    Status run_middleware_route(session::ExploitSession &session,
                                const memory::WriteRequest &request) {
        (void) session;
        return race::run_main_route_threads(request);
    }
} // namespace ghostlock::route::middleware
