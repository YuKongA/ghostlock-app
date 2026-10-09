#pragma once
/** @file backend/cve_2026_43499/access_params.hpp
 *  What this backend's kernel access needs besides an address and a value: which route, and
 *  where in it.
 *
 *  The route is a parameter rather than a separate object on purpose. The user's rule: one
 *  KernelAccess per backend, because the id that names it is a backend and not a route -- a
 *  route object per route would turn the route into an assembly axis, which it is not. So the
 *  caller says which route it wants in the parameters, the access brings that route up inside
 *  initialize and reclaims it in shutdown, and construction does none of that work.
 *
 *  Lifetime that follows from this: the route lives from initialize to shutdown and serves every
 *  access in between. The window inside one access is still scoped to that access -- it is a race
 *  scope and reclaims on every exit path -- but the route, its carrier and the landing gear it
 *  set up outlive the individual write. Switching routes means shutting down and initializing
 *  again, which is why the state a capability reports has a Closed as well as a Declared.
 */
#include <cstdint>

#include "contract/kernel_access.hpp"
#include "profile/route_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** Parameters for this backend. The route is what initialize reads; the offset is what an
     *  access reads. Both travel in the same object so a caller can set them once and keep
     *  using it, and an entry point refuses what it needs but does not find rather than
     *  substituting a default. */
    class AccessParams final : public contract::KernelAccess::AccessMiscParameters {
    public:
        /** The route the access should use. Unset means not decided, which initialize refuses. */
        profile::RouteId route = profile::RouteId::Unset;
        /** Where in that route to write. Zero is a legitimate offset, so it is not a sentinel. */
        std::uint64_t offset = 0U;
        /** Whether the offset has been given at all, since zero is legitimate. */
        bool offset_set = false;

        [[nodiscard]] std::string_view name() const noexcept override { return "cve_2026_43499"; }

        /** Whether this set is complete enough for initialize: it needs a route. */
        [[nodiscard]] constexpr bool usable_for_initialize() const noexcept {
            return route != profile::RouteId::Unset;
        }
        /** Whether this set is complete enough for a write: it needs both. */
        [[nodiscard]] constexpr bool usable_for_access() const noexcept {
            return usable_for_initialize() && offset_set;
        }
    };
}  // namespace ghostlock::backend::cve_2026_43499
