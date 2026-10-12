#pragma once
/** @file backend/cve_2026_43499/access_factory_43499.hpp
 *  The 43499 factory: one kernel access per backend, chosen by route.
 *
 *  initialize carries the route in the parameters, but the object a caller holds
 *  is the route's own subclass, so the route's parameter type and its wrapper are
 *  decided once at construction instead of being re-read on every write.
 */
#include <memory>

#include "backend/cve_2026_43499/kernel_access.hpp"
#include "profile/route_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** The access for a route, or null when the route is Unset or unknown.
     *  @param route The route the caller intends to initialize with.
     *  @return An instance of that route's subclass; null when no route was named. */
    [[nodiscard]] std::unique_ptr<contract::KernelAccess> create_kernel_access_43499(
        profile::RouteId route) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499
