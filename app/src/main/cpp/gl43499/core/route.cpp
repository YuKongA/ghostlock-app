/**
 * @file route.cpp
 * @brief 路由控制器与 middleware 入口（自 core.cpp 按用途拆分，逐字保留）。
 */
#include "route.h"
#include "race.h"
#include "session.h"

/* ==== route/route_controller.cpp ==== */


namespace ghostlock::route {
    void RouteController::init(ghostlock::race::PiRace *race_ptr,
                               const profile::TargetProfile *profile_ptr) {
        race = race_ptr;
        profile = profile_ptr;
        fallback_used = 0;
    }

    RouteStatus RouteController::execute(const memory::WriteRequest *request) {
        if (!race || !profile || !request) {
            return RouteStatus{.code = ROUTE_UNSUPPORTED};
        }
        const RouteRunResult result = run_route(*profile, request, 1);
        fallback_used = result.fallback_used ? 1 : 0;
        return result.status;
    }
} // namespace ghostlock::route

/* ==== route/route_middleware.cpp ==== */
/*
 * GhostLock — middleware route entry (Batch 4, D1=B slice 3b).
 *
 * Single route write, moved out of the retired procedure; behavior is
 * unchanged (the route policy and race still run inside run_main_route_threads).
 */



namespace ghostlock::route::middleware {
    Status run_middleware_route(session::ExploitSession &session,
                                const memory::WriteRequest &request) {
        (void) session;
        return race::run_main_route_threads_from_session(request);
    }
} // namespace ghostlock::route::middleware
