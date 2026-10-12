/** @file backend/cve_2026_43499/seam/multicast_route_linux.cpp
 *  The device policy, checked against the concept the route requires.
 *
 *  Nothing is defined here. The file exists so that the header is compiled by the same zero-warning
 *  build the device binary uses, and the assertion below is the promise the route's own header makes:
 *  a policy that stops providing one of the calls stops this build where the policy is written,
 *  rather than at the point where the route uses it.
 */
#include "backend/cve_2026_43499/seam/multicast_route_linux.hpp"

#if defined(__linux__)
#include "backend/cve_2026_43499/seam/multicast_route.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
    static_assert(RouteOps<LinuxRouteOps>,
                  "the device policy provides every call one multicast attempt makes");
}  // namespace ghostlock::backend::cve_2026_43499::seam
#endif
