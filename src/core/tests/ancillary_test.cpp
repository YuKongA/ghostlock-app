/* Host test for the ancillary controller skeleton: the behavior registry, the
 * profile gate and the stage-dispatch signature. No session is constructed and
 * no behavior body runs yet, so this links without the Android write path. */

#include "session/ancillary/ancillary_controller.hpp"

#include <cassert>
#include <optional>
#include <concepts>
#include <cstdio>
#include <tuple>

using namespace ghostlock;

namespace {
    struct EmptyMiddleware final {};

    /* A behavior that opts in, to exercise the enabled-traversal independent of
     * the (currently off) registered VrGuardPolicy. */
    struct OnPolicy : session::ancillary::AncillaryPolicyDefaults {
        static constexpr session::ancillary::AncillaryKind kind =
                session::ancillary::AncillaryKind::VrGuard;

        static bool enabled(const profile::TargetProfile &) noexcept {
            return true;
        }
    };

    profile::TargetProfile make_profile() {
        static profile::kernel_offsets values;
        values = {};
        return profile::TargetProfile::from(&values);
    }
} // namespace

int main() {
    using namespace session::ancillary;

    /* Both the registered behavior and an opting-in one satisfy the contract. */
    static_assert(AncillaryPolicyFor<VrGuardPolicy, EmptyMiddleware>);
    static_assert(AncillaryPolicyFor<OnPolicy, EmptyMiddleware>);

    /* The registry currently holds exactly the vr.ko guard. */
    static_assert(std::tuple_size_v<AncillaryPolicyList> == 1);

    int visited = 0;
    bool saw_vr_guard = false;
    for_each_ancillary_policy([&]<class P>() {
        ++visited;
        if (P::kind == AncillaryKind::VrGuard) saw_vr_guard = true;
    });
    assert(visited == 1 && saw_vr_guard);

    const profile::TargetProfile profile = make_profile();

    /* Skeleton: the registered behavior is gated off, so nothing dispatches. */
    assert(!VrGuardPolicy::enabled(profile));
    int enabled_visited = 0;
    for_each_enabled_ancillary_policy(profile, [&]<class P>() { ++enabled_visited; });
    assert(enabled_visited == 0);

    /* An opting-in behavior is reached by the same traversal. */
    assert(OnPolicy::enabled(profile));

    /* Ancillary vr.ko guard: the gate, the fail-closed plan, and the pure
     * target arithmetic the device write performs. */
    profile::kernel_offsets vr_values{};
    vr_values.meta.vr_guard = 1;
    vr_values.offsets.vr_sys_exit_tp = 0x021a1020; /* measured on vivo 6.1 */
    vr_values.geometry.vr_tracepoint_funcs = 0x40; /* BTF: sizeof(tracepoint) 0x48 */
    const profile::TargetProfile vr_profile = profile::TargetProfile::from(&vr_values);
    assert(VrGuardPolicy::enabled(vr_profile));
    const std::optional<VrGuardPlan> vr_plan = plan_vr_guard(vr_profile);
    assert(vr_plan.has_value());
    assert(vr_plan->image_offset == 0x021a1020u + 0x40u);
    assert(vr_plan->width_bytes == sizeof(uintptr_t));

    /* The gate and the layout are separate facts (guide §5): the plan is pure
     * arithmetic over the layout, so it still describes the write when the gate
     * is off — it is `enabled()` that keeps a gated-off behavior from running. */
    profile::kernel_offsets vr_no_gate = vr_values;
    vr_no_gate.meta.vr_guard = 0;
    const profile::TargetProfile vr_no_gate_profile =
            profile::TargetProfile::from(&vr_no_gate);
    assert(!VrGuardPolicy::enabled(vr_no_gate_profile));
    assert(plan_vr_guard(vr_no_gate_profile).has_value());

    /* Fail closed: either fact missing means no plan at all, so a profile that
     * describes no tracepoint can never produce a write. */

    profile::kernel_offsets vr_no_funcs = vr_values;
    vr_no_funcs.geometry.vr_tracepoint_funcs = std::nullopt;
    assert(!VrGuardPolicy::enabled(profile::TargetProfile::from(&vr_no_funcs)));

    profile::kernel_offsets vr_no_symbol = vr_values;
    vr_no_symbol.offsets.vr_sys_exit_tp = 0;
    assert(!VrGuardPolicy::enabled(profile::TargetProfile::from(&vr_no_symbol)));

    /* The controller exposes the stage entry the backend calls. */
    static_assert(
        requires(session::ExploitSession &session, AncillaryContext &context) {
            {
                AncillaryController<EmptyMiddleware>::apply(
                    AncillaryStage::PreSpawn, session, context)
            } -> std::same_as<Status>;
        });

    puts("ancillary_test: ok");
    return 0;
}
