/* Host test for the platform::vivo vendor behaviors (ADR-0004 R4): the
 * profile->View adapter, the policy gates and the pure plan arithmetic the
 * device writes perform. No session is constructed and no behavior body runs;
 * the neutral mechanism's own contract is covered by ancillary_test.cpp. */

#include "backend/cve_2026_43499/capability_adapters.hpp"
#include "platform/vivo/registry.hpp"
#include "platform/vivo/schema.hpp"
#include "platform/vivo/vr_guard.hpp"
#include "platform/vivo/vr_task_tag.hpp"

#include <cassert>
#include <concepts>
#include <cstdio>
#include <optional>
#include <tuple>
#include <type_traits>

using namespace ghostlock;

int main() {
    using namespace platform::vivo;

    /* The vendor behaviors satisfy the neutral contract; the mechanism never
     * sees their kind. */
    static_assert(plugin::PluginPolicyFor<VrGuardPolicy, View>);
    static_assert(plugin::PluginPolicyFor<VrTaskTagPolicy, View>);
    static_assert(PluginKind::VrGuard != PluginKind::VrTaskTag);

    /* The registry the backend injects is exactly these two behaviors. */
    static_assert(std::is_same_v<VivoPluginPolicies,
                                 std::tuple<VrGuardPolicy, VrTaskTagPolicy>>);

    /* profile -> View: the three vendor facts, no backend/session type. */
    profile::kernel_offsets vr_values{};
    vr_values.misc.vr_guard = 1;
    vr_values.misc.vr_sys_exit_tp = 0x021a1020; /* measured on vivo 6.1 */
    vr_values.misc.vr_tracepoint_funcs = 0x40;  /* BTF: sizeof(tracepoint) 0x48 */
    const View vr_view = make_view(profile::TargetProfile::from(&vr_values));
    assert(vr_view.guard_enabled);
    assert(vr_view.sys_exit_tp == 0x021a1020u);
    assert(vr_view.tracepoint_funcs == 0x40u);

    /* vr.ko guard: the gate, the fail-closed plan, and the pure target
     * arithmetic the device write performs. */
    assert(VrGuardPolicy::enabled(vr_view));
    const std::optional<VrGuardPlan> vr_plan = plan_vr_guard(vr_view);
    assert(vr_plan.has_value());
    assert(vr_plan->image_offset == 0x021a1020u + 0x40u);
    assert(vr_plan->width_bytes == sizeof(uintptr_t));

    /* The gate and the layout are separate facts (guide section 5): the plan is
     * pure arithmetic over the layout, so it still describes the write when the
     * gate is off; enabled() is what keeps a gated-off behavior from running. */
    profile::kernel_offsets vr_no_gate = vr_values;
    vr_no_gate.misc.vr_guard = 0;
    const View no_gate_view = make_view(profile::TargetProfile::from(&vr_no_gate));
    assert(!VrGuardPolicy::enabled(no_gate_view));
    assert(plan_vr_guard(no_gate_view).has_value());

    /* Fail closed: either fact missing means no plan at all, so a view that
     * describes no tracepoint can never produce a write. */
    profile::kernel_offsets vr_no_funcs = vr_values;
    vr_no_funcs.misc.vr_tracepoint_funcs = 0;
    const View no_funcs_view = make_view(profile::TargetProfile::from(&vr_no_funcs));
    assert(!VrGuardPolicy::enabled(no_funcs_view));
    assert(!plan_vr_guard(no_funcs_view).has_value());

    profile::kernel_offsets vr_no_symbol = vr_values;
    vr_no_symbol.misc.vr_sys_exit_tp = 0;
    const View no_symbol_view = make_view(profile::TargetProfile::from(&vr_no_symbol));
    assert(!VrGuardPolicy::enabled(no_symbol_view));
    assert(!plan_vr_guard(no_symbol_view).has_value());

    /* vr.ko per-task tag removal: it shares the guard's profile gate but needs
     * no per-image facts, so a gated profile with no tracepoint layout still
     * enables it. */
    assert(VrTaskTagPolicy::enabled(vr_view));
    assert(VrTaskTagPolicy::enabled(no_funcs_view));
    assert(VrTaskTagPolicy::enabled(no_symbol_view));
    profile::kernel_offsets empty_values{};
    const View empty_view = make_view(profile::TargetProfile::from(&empty_values));
    assert(!VrTaskTagPolicy::enabled(empty_view));

    /* The pure plan is the two address arithmetic steps the device write
     * performs: the flags word at child_task+0 and tag B aligned down from
     * child_task+VR_TAG_B_OFF. */
    const uintptr_t child_task = 0xffff000012340000ULL;
    const VrTaskTagPlan tag_plan = plan_vr_task_tag(child_task);
    assert(tag_plan.flags_word == child_task + 0u);
    assert(tag_plan.tag_b_word ==
           ((child_task + 0x2cu) & ~static_cast<uintptr_t>(7)));
    assert((tag_plan.tag_b_word & 7u) == 0);

    /* ChildTask capability: a not-ready child is reported as an explicit
     * CapabilityError::Unavailable, and the vr_task_tag resolver converts that
     * into the fail-soft skip (nullopt) rather than acting on the old magic 0. */
    using ghostlock::backend::cve_2026_43499::StepChildTask;
    const StepChildTask missing_child{0};
    const contract::CapabilityResult<std::uint64_t> missing = missing_child.current();
    assert(!missing.has_value());
    assert(missing.error() == contract::CapabilityError::Unavailable);

    const StepChildTask ready_child{child_task};
    const contract::CapabilityResult<std::uint64_t> ready = ready_child.current();
    assert(ready.has_value());
    assert(*ready == static_cast<std::uint64_t>(child_task));

    const contract::Capabilities no_child{};
    assert(!resolve_vr_task_tag_target(no_child).has_value());

    StepChildTask not_ready{0};
    const contract::Capabilities not_ready_caps{.child = &not_ready};
    assert(!resolve_vr_task_tag_target(not_ready_caps).has_value());

    StepChildTask ready_task{child_task};
    const contract::Capabilities ready_caps{.child = &ready_task};
    const std::optional<uintptr_t> resolved = resolve_vr_task_tag_target(ready_caps);
    assert(resolved.has_value());
    assert(*resolved == child_task);

    puts("platform_vivo_test: ok");
    return 0;
}
