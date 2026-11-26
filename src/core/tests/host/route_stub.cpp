#include "host_attack_script.hpp"
#include "route/route_api.hpp"
#include "route/route_middleware.hpp"

#include "session/exploit_session.hpp"

/* Host stubs for the middleware route. The real PI race never runs; the
 * scripted Status stands in for "verified write". */
namespace lsprivilege::route::middleware {
    Status run_middleware_route(session::ExploitSession &session,
                                const memory::WriteRequest &request) {
        (void) session;
        (void) request;
        host::script().record("route");
        return host::script().next_route();
    }
} // namespace lsprivilege::route::middleware

namespace lsprivilege::route {
    void reserve_standard_io(void) {}

    RouteStatus do_pselect_fake_lock_route(const lsprivilege::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }

    RouteStatus do_tcp_fake_lock_route(const lsprivilege::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }

    RouteStatus do_kernel5_fake_lock_route(const lsprivilege::memory::WriteRequest *request) {
        (void) request;
        return RouteStatus{};
    }
} // namespace lsprivilege::route
