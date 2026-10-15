/** @file backend/cve_2026_43499/landing/multicast_route_linux.cpp
 *  The device policy, checked against the concept the route requires.
 *
 *  Nothing is defined here. The file exists so that the header is compiled by the same zero-warning
 *  build the device binary uses, and the assertion below is the promise the route's own header makes:
 *  a policy that stops providing one of the calls stops this build where the policy is written,
 *  rather than at the point where the route uses it.
 */
#include "backend/cve_2026_43499/landing/multicast_route_linux.hpp"

#if defined(__linux__)
#include "backend/cve_2026_43499/landing/multicast_route.hpp"

namespace ghostlock::backend::cve_2026_43499::landing {
    static_assert(RouteOps<LinuxRouteOps>,
                  "the device policy provides every call one multicast attempt makes");
    /* 路由与窗口的编译期对接（2026-10-12 裁定）：窗口以具体路由类型为模板实参直接调用，
     *  所以「路由满足窗口的 concept」这条判据在这里成立——不满足则本行先编译不过。 */
    static_assert(RouteExec<MulticastRoute<LinuxRouteOps>>,
                  "the device route provides what the window asks of its controller");
}  // namespace ghostlock::backend::cve_2026_43499::landing
#endif
