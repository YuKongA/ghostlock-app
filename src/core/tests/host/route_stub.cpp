#include "host_attack_script.hpp"
#include "backend/cve_2026_43499/route/route_api.hpp"
#include "backend/cve_2026_43499/route/route_middleware.hpp"

#include "session/core_session.hpp"

/* Host stubs for the middleware route. The real PI race never runs; the
 * scripted Status stands in for "verified write". */
namespace ghostlock::backend::cve_2026_43499::route::middleware {
    Status run_middleware_route(session::CoreSession &session,
                                const memory::WriteRequest &request) {
        (void) session;
        (void) request;
        host::script().record("route");
        return host::script().next_route();
    }
} // namespace ghostlock::backend::cve_2026_43499::route::middleware

namespace ghostlock::backend::cve_2026_43499::route {
    void reserve_standard_io(void) {}

    RouteStatus do_pselect_fake_lock_route(const ghostlock::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }

    RouteStatus do_tcp_fake_lock_route(const ghostlock::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }

    RouteStatus do_kernel5_fake_lock_route(const ghostlock::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }
} // namespace ghostlock::backend::cve_2026_43499::route
