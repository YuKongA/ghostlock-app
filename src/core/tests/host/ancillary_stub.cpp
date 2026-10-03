#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "ancillary/ancillary_controller.hpp"

/* Host stubs for the ancillary behaviors. The backend compiles the real
 * AncillaryController call sites, but on host the behaviors' Android-only
 * bodies (kernel writes and /proc/modules probes) are not linked; these
 * definitions stand in so the data-flow binary links. The controller still
 * gates each behavior on the profile, so a default (unloaded) profile never
 * dispatches here, and the values do not affect control flow. */
namespace ghostlock::ancillary {
    template <class Middleware>
    Status VrGuardPolicy::apply(AncillaryStage, CoreSession &, AncillaryContext &) noexcept {
        return true;
    }

    template Status VrGuardPolicy::apply<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(AncillaryStage, CoreSession &,
                                                              AncillaryContext &) noexcept;
    template Status VrGuardPolicy::apply<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(AncillaryStage, CoreSession &,
                                                           AncillaryContext &) noexcept;
    template Status VrGuardPolicy::apply<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(AncillaryStage, CoreSession &,
                                                                 AncillaryContext &) noexcept;

    template <class Middleware>
    Status VrTaskTagPolicy::apply(AncillaryStage, CoreSession &, AncillaryContext &) noexcept {
        return true;
    }

    template Status VrTaskTagPolicy::apply<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(AncillaryStage, CoreSession &,
                                                                AncillaryContext &) noexcept;
    template Status VrTaskTagPolicy::apply<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(AncillaryStage, CoreSession &,
                                                             AncillaryContext &) noexcept;
    template Status VrTaskTagPolicy::apply<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(AncillaryStage,
                                                                   CoreSession &,
                                                                   AncillaryContext &) noexcept;
} // namespace ghostlock::ancillary
