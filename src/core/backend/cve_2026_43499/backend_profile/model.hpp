#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_MODEL_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_MODEL_HPP

/* cve_2026_43499 backend model vocabulary (A2-4-4).
 *
 * The canonical definitions live in contract/model.hpp. kernel_offsets embeds
 * execution_settings in its frozen (sizeof == 520) layout, and TargetProfile
 * returns the route/layout types, so the embedded field types cannot move
 * behind a backend-only header without either changing the byte layout or
 * creating a contract -> backend include edge. ADR-0004 R1 forbids the latter
 * and the A2-4-4 invariants forbid the former.
 *
 * This header is the backend's single include for the frozen model and
 * re-exports the 43499 policy vocabulary under the backend_profile namespace,
 * so backend code names its own types locally while the physical definition
 * stays neutral. */
#include "contract/model.hpp"

namespace ghostlock::backend::cve_2026_43499::backend_profile {
    using ghostlock::profile::execution_settings;
    using ghostlock::profile::RouteKind;
    using ghostlock::profile::RouteCatalogEntry;
    using ghostlock::profile::kRouteCatalog;
    using ghostlock::profile::route_kind_from_string;
    using ghostlock::profile::kRouteAuto;
    using ghostlock::profile::kRouteTcpZerocopy;
    using ghostlock::profile::kRouteSelectStack;
    using ghostlock::profile::kRouteMulticastWaiter;
    using ghostlock::profile::McastTuning;
} // namespace ghostlock::backend::cve_2026_43499::backend_profile

#endif
