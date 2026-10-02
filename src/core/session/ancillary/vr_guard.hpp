#ifndef GHOSTLOCK_VR_GUARD_HPP
#define GHOSTLOCK_VR_GUARD_HPP

#include <cstdint>
#include <optional>

#include "profile/model.h"
#include "session/ancillary/ancillary_policy.hpp"

namespace ghostlock::session::ancillary {
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
     * Two facts come from the profile and both must be present (see `plan()`):
     * the symbol's image offset, and `offsetof(struct tracepoint, funcs)`.
     * Neither is inferred from the kernel version — 6.6 gained a `probestub`
     * member ahead of `funcs` and moved it — so the struct offset is derived per
     * image (BTF) by the extractor.
     *
     * When it runs: `PreSpawn` — SELinux is permissive and no victim exists yet,
     * so one write covers every process the run will bring up, ksud included.
     */

    /* Pure plan: profile -> write target and width, or nothing.
     *
     * `nullopt` means the profile does not carry both facts, which is the
     * fail-closed path: a non-vivo profile never triggers a write. The target is
     * image-relative; turning it into a direct-map alias needs the session and
     * stays in `apply()`. Host-testable (see src/core/tests/ancillary_test.cpp).
     */
    struct VrGuardPlan final {
        uint64_t image_offset = 0;
        uint32_t width_bytes = 0;
    };

    [[nodiscard]] inline std::optional<VrGuardPlan> plan_vr_guard(
        const profile::TargetProfile &profile) noexcept {
        const uint64_t tracepoint = profile.vr_sys_exit_tp();
        const profile::VrGuardLayout layout = profile.vr_guard_layout();
        if (tracepoint == 0 || !layout.tracepoint_funcs.has_value()) return std::nullopt;
        return VrGuardPlan{
                .image_offset = tracepoint + *layout.tracepoint_funcs,
                /* funcs is a pointer array; the entry the iterator reads is one
                 * word wide. */
                .width_bytes = static_cast<uint32_t>(sizeof(uintptr_t)),
        };
    }

    struct VrGuardPolicy : AncillaryPolicyDefaults {
        static constexpr AncillaryKind kind = AncillaryKind::VrGuard;

        /* Profile gate only (guide §5): it says the support list enables the
         * behavior for this profile. Whether vr.ko is present on the running
         * device is decided at apply() time. */
        static bool enabled(const profile::TargetProfile &profile) noexcept {
            return profile.vr_guard_enabled() && plan_vr_guard(profile).has_value();
        }

        template <class Middleware>
        static Status apply(AncillaryStage stage, ExploitSession &session,
                            AncillaryContext &context) noexcept;
    };
} // namespace ghostlock::session::ancillary

#endif
