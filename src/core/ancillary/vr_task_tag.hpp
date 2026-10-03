#ifndef GHOSTLOCK_VR_TASK_TAG_HPP
#define GHOSTLOCK_VR_TASK_TAG_HPP

#include <cstdint>

#include "memory/target.h"
#include "profile/macros.h"
#include "profile/model.h"
#include "ancillary/ancillary_policy.hpp"

namespace ghostlock::ancillary {
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
     * `profile/macros.h`), not profile facts: unlike the guard there is no
     * per-image layout to resolve.
     *
     * When it runs: `PostSpawn` - the rooted child exists but W2 verify has not
     * yet read its uid. The backend sets `AncillaryContext::child_task`.
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

    struct VrTaskTagPolicy : AncillaryPolicyDefaults {
        static constexpr AncillaryKind kind = AncillaryKind::VrTaskTag;

        /* Profile gate only: the support list says the behavior applies for this
         * profile. Whether vr.ko is present on the running device is decided at
         * apply() time. Shares the guard's gate (guide §5, no new GLK1 field). */
        static bool enabled(const profile::TargetProfile &profile) noexcept {
            return profile.vr_guard_enabled();
        }

        template <class Middleware>
        static Status apply(AncillaryStage stage, CoreSession &session,
                            AncillaryContext &context) noexcept;
    };
} // namespace ghostlock::ancillary

#endif
