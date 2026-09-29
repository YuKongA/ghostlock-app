/* Host test for the ancillary controller skeleton: the behavior registry, the
 * profile gate and the stage-dispatch signature. No session is constructed and
 * no behavior body runs yet, so this links without the Android write path. */

#include "session/ancillary/ancillary_controller.hpp"

#include <cassert>
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

    /* The controller exposes the stage entry the backend will call in stage C. */
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
