#ifndef GHOSTLOCK_VR_GUARD_HPP
#define GHOSTLOCK_VR_GUARD_HPP

#include "session/ancillary/ancillary_policy.hpp"

namespace ghostlock::session::ancillary {
    /* vivo/iQOO vr.ko anti-root neutralization — the first registered behavior.
     *
     * Skeleton: the behavior is declared and registered but gated off. The
     * profile wiring and the write/read steps land with the vr.ko work. */
    struct VrGuardPolicy : AncillaryPolicyDefaults {
        static constexpr AncillaryKind kind = AncillaryKind::VrGuard;

        static bool enabled(const profile::TargetProfile &) noexcept {
            return false;
        }
    };
} // namespace ghostlock::session::ancillary

#endif
