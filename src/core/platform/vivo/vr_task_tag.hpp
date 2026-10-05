#ifndef GHOSTLOCK_PLATFORM_VIVO_VR_TASK_TAG_HPP
#define GHOSTLOCK_PLATFORM_VIVO_VR_TASK_TAG_HPP

#include <cstdint>
#include <optional>

#include "plugin/policy.hpp"
#include "memory/target.h"
#include "platform/vivo/kind.hpp"
#include "platform/vivo/macros.h"
#include "platform/vivo/schema.hpp"

namespace ghostlock::platform::vivo {
    using ghostlock::plugin::PluginStage;
    using ghostlock::session::CoreSession;

    /* Ancillary behavior: vivo/iQOO `vr.ko` per-task tag removal.
     *
     * vr.ko tags every app-origin task at fork/clone time. Once such a task
     * holds euid 0, vr's `sys_exit` tracepoint probe kills it on the way out.
     * The guard behavior (PreSpawn) disables the probe globally; this behavior
     * is the per-task belt-and-suspenders for the rooted child itself, and
     * exists because the child can hold uid 0 before its task is found, so its
     * tag must be stripped before W2 verify runs the child's getuid().
     *
     * The write is 64-bit granular:
     *   - task+0x00 (thread_info.flags) covers tag A at +0x06 and also clears
     *     the syscall-tracepoint bit (0x400), taking the task off the sys_exit
     *     slow path immediately;
     *   - tag B is at +0x2c, aligned down to +0x28 and zeroed whole.
     * Both offsets are compile-time constants (`memory/target.h`,
     * `platform/vivo/macros.h`), not profile facts: unlike the guard there is
     * no per-image layout to resolve.
     *
     * When it runs: `PostSpawn` - the rooted child exists but W2 verify has not
     * yet read its uid. The backend provides a `contract::ChildTask`
     * capability so a not-ready child is an explicit error, not the magic 0.
     */

    /* Pure plan: child task -> the two words to zero. Host-testable. */
    struct VrTaskTagPlan final {
        uintptr_t flags_word = 0;
        uintptr_t tag_b_word = 0;
    };

    [[nodiscard]] inline VrTaskTagPlan plan_vr_task_tag(uintptr_t child_task) noexcept {
        return VrTaskTagPlan{
                .flags_word = child_task +
                              static_cast<uintptr_t>(memory::TASK_THREAD_INFO_FLAGS_OFF),
                .tag_b_word = (child_task + static_cast<uintptr_t>(VR_TAG_B_OFF)) &
                              ~static_cast<uintptr_t>(7),
        };
    }

    /* Resolve the rooted child's task through the capability. `nullopt` is the
     * fail-soft skip path: the capability is absent, or it reports the child is
     * not ready (CapabilityError::Unavailable). This replaces the old
     * `child_task == 0` convention with the section 3.9 conclusion 4 state
     * query, and stays host-testable (no device types). */
    [[nodiscard]] inline std::optional<uintptr_t> resolve_vr_task_tag_target(
        const contract::Capabilities &capabilities) noexcept {
        if (capabilities.child == nullptr) return std::nullopt;
        const contract::CapabilityResult<std::uint64_t> task =
                capabilities.child->current();
        if (!task.has_value() || task.value() == 0) return std::nullopt;
        return static_cast<uintptr_t>(task.value());
    }

    /* Device-only execution body (defined in vr_task_tag.cpp on Android, stubbed
     * on host). */
    Status execute_vr_task_tag(PluginStage stage,
                               const contract::Capabilities &capabilities) noexcept;

    struct VrTaskTagPolicy : plugin::PluginPolicyDefaults {
        static constexpr PluginKind kind = PluginKind::VrTaskTag;

        /* View gate only: the support list says the behavior applies for this
         * profile. Whether vr.ko is present on the running device is decided at
         * execution time. Shares the guard's gate (guide section 5, no new GLK1
         * field). */
        static bool enabled(const View &view) noexcept {
            return view.guard_enabled;
        }

        static Status apply(PluginStage stage, CoreSession &session,
                            const contract::Capabilities &capabilities,
                            const View &) noexcept {
            (void)session;
            return execute_vr_task_tag(stage, capabilities);
        }
    };
} // namespace ghostlock::platform::vivo

#endif
