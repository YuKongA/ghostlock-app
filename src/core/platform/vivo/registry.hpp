#ifndef GHOSTLOCK_PLATFORM_VIVO_REGISTRY_HPP
#define GHOSTLOCK_PLATFORM_VIVO_REGISTRY_HPP

#include <tuple>

#include "platform/vivo/vr_guard.hpp"
#include "platform/vivo/vr_task_tag.hpp"

namespace ghostlock::platform::vivo {
    /* The vivo vendor behaviors, injected by the backend call site as one
     * caller-supplied policy list (ADR-0004 R14: compile-time list, no runtime
     * registry). */
    using VivoPluginPolicies =
            std::tuple<VrGuardPolicy, VrTaskTagPolicy>;
} // namespace ghostlock::platform::vivo

#endif
