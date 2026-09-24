#ifndef GHOSTLOCK_ROUTE_MIDDLEWARE_HPP
#define GHOSTLOCK_ROUTE_MIDDLEWARE_HPP

#include "memory/payload_builder.h"
#include "session/exploit_session.hpp"
#include "support/status.hpp"

namespace ghostlock::route::middleware {
    /* Batch 4 (D1=B slice 3b) middleware entry for one route write. This is the
     * route execution previously inlined in ExploitProcedure::attack_write; the
     * route/race behavior is unchanged (run_main_route_threads still selects and
     * runs the route policy). */
    Status run_middleware_route(session::ExploitSession &session,
                                const memory::WriteRequest &request);
} // namespace ghostlock::route::middleware

#endif
