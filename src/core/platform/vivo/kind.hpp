#ifndef GHOSTLOCK_PLATFORM_VIVO_KIND_HPP
#define GHOSTLOCK_PLATFORM_VIVO_KIND_HPP

#include <cstdint>

namespace ghostlock::platform::vivo {
    /* Vendor behavior ids (vivo vr.ko guard / per-task tag). These are vendor
     * vocabulary: they live with the vendor behaviors, not in the neutral
     * ancillary mechanism (ADR-0004 R4). The mechanism never branches on them;
     * a policy only carries its id for identification. */
    enum class AncillaryKind : std::uint8_t {
        VrGuard = 1,
        VrTaskTag = 2,
    };
} // namespace ghostlock::platform::vivo

#endif
