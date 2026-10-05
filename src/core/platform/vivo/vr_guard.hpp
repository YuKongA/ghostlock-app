#ifndef GHOSTLOCK_PLATFORM_VIVO_VR_GUARD_HPP
#define GHOSTLOCK_PLATFORM_VIVO_VR_GUARD_HPP

#include <cstdint>
#include <optional>

#include "plugin/policy.hpp"
#include "platform/vivo/kind.hpp"
#include "platform/vivo/schema.hpp"

namespace ghostlock::platform::vivo {
    using ghostlock::plugin::PluginStage;
    using ghostlock::session::CoreSession;

    /* Ancillary behavior: vivo/iQOO `vr.ko` anti-root neutralization.
     *
     * vr.ko registers an enforcement probe on `__tracepoint_sys_exit`. Once a
     * process holds uid 0, vr's `commit_creds` probe tags that task and the
     * `sys_exit` probe kills it on the way out, which is what takes ksud and
     * every shell it spawns down (black-screened apps, an unusable manager).
     * Clearing `__tracepoint_sys_exit.funcs` makes the tracepoint iterator skip
     * every probe for every process; the tagging probe still runs, but nothing
     * acts on the tag.
     *
     * The two facts come from the caller's View and both must be present (see
     * `plan_vr_guard()`): the symbol's image offset, and offsetof(struct
     * tracepoint, funcs). Neither is inferred from the kernel version - 6.6
     * gained a `probestub` member ahead of `funcs` and moved it - so the
     * struct offset is derived per image (BTF) by the extractor.
     *
     * When it runs: `PreSpawn` - SELinux is permissive and no victim exists yet,
     * so one write covers every process the run will bring up, ksud included.
     */

    /* Pure plan: view -> write target and width, or nothing.
     *
     * `nullopt` means the view does not carry both facts, which is the
     * fail-closed path: a non-vivo profile never triggers a write. The target is
     * image-relative; turning it into a direct-map alias needs the injected
     * `contract::KernelAlias` and stays in `execute_vr_guard()`.
     * Host-testable (see src/core/tests/platform_vivo_test.cpp). */
    struct VrGuardPlan final {
        uint64_t image_offset = 0;
        uint32_t width_bytes = 0;
    };

    [[nodiscard]] inline std::optional<VrGuardPlan> plan_vr_guard(
        const View &view) noexcept {
        if (view.sys_exit_tp == 0 || view.tracepoint_funcs == 0) return std::nullopt;
        return VrGuardPlan{
                .image_offset = view.sys_exit_tp + view.tracepoint_funcs,
                /* funcs is a pointer array; the entry the iterator reads is one
                 * word wide. */
                .width_bytes = static_cast<uint32_t>(sizeof(uintptr_t)),
        };
    }

    /* Device-only execution body (defined in vr_guard.cpp on Android, stubbed on
     * host). The behavior reaches the kernel through the injected capability
     * aggregate, so this module still names no backend route type. */
    Status execute_vr_guard(PluginStage stage,
                            const contract::Capabilities &capabilities,
                            const View &view) noexcept;

    struct VrGuardPolicy : plugin::PluginPolicyDefaults {
        static constexpr PluginKind kind = PluginKind::VrGuard;

        /* View gate only (guide section 5): it says the support list enables the
         * behavior for this profile. Whether vr.ko is present on the running
         * device is decided at execution time. */
        static bool enabled(const View &view) noexcept {
            return view.guard_enabled && plan_vr_guard(view).has_value();
        }

        static Status apply(PluginStage stage, CoreSession &session,
                            const contract::Capabilities &capabilities,
                            const View &view) noexcept {
            (void)session;
            return execute_vr_guard(stage, capabilities, view);
        }
    };
} // namespace ghostlock::platform::vivo

#endif
