#include "platform/vivo/vr_guard.hpp"
#include "platform/vivo/vr_task_tag.hpp"

/* Host stubs for the platform::vivo device execution bodies. The backend
 * compiles the real PluginController call sites and the policies' inline
 * apply; on host the Android-only bodies (kernel writes and /proc/modules
 * probes) are not compiled, so these definitions stand in for the data-flow
 * binary. The call-site gate still decides which behaviors run, so a default
 * (unloaded) profile never dispatches here, and the return values do not affect
 * control flow. */
namespace ghostlock::platform::vivo {
    Status execute_vr_guard(PluginStage,
                            const contract::Capabilities &, const View &) noexcept {
        return true;
    }

    Status execute_vr_task_tag(PluginStage,
                               const contract::Capabilities &) noexcept {
        return true;
    }
} // namespace ghostlock::platform::vivo
