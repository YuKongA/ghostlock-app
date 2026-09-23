/* Host test for the route-policy registry: compile-time capabilities, policy
 * selection, direct dispatch and the declared fallback.
 *
 * White box: capabilities are asserted per policy (including the inherited
 * RoutePolicyDefaults) and every dispatch path is counted through stubs. */

#include "route/route_policy.hpp"

#include <cassert>
#include <cstdio>
#include <variant>

using namespace ghostlock;

namespace {
    struct StubState {
        route::RouteStatus select_status = {.code = route::ROUTE_OK};
        route::RouteStatus tcp_status = {.code = route::ROUTE_OK};
        route::RouteStatus multicast_status = {.code = route::ROUTE_OK};
        int32_t select_calls = 0;
        int32_t tcp_calls = 0;
        int32_t multicast_calls = 0;
    };

    StubState state;

    void reset() {
        state = StubState{};
    }

    profile::TargetProfile profile_with(uint8_t route, uint8_t fallback) {
        static profile::kernel_offsets values;
        values = {};
        values.route = route;
        values.fallback_route = fallback;
        return profile::TargetProfile::from(&values);
    }
} // namespace

namespace ghostlock::route {
    RouteStatus do_pselect_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        state.select_calls++;
        return state.select_status;
    }

    RouteStatus do_tcp_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        state.tcp_calls++;
        return state.tcp_status;
    }

    RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        assert(request);
        state.multicast_calls++;
        return state.multicast_status;
    }
} // namespace ghostlock::route

int32_t main(void) {
    using namespace ghostlock::route;
    using ghostlock::profile::RouteKind;

    /* ---- White box: capabilities, including the base-class defaults. ---- */
    static_assert(SelectPolicy::kind == RouteKind::SelectStack);
    static_assert(TcpPolicy::kind == RouteKind::TcpZerocopy);
    static_assert(MulticastPolicy::kind == RouteKind::MulticastWaiter);

    static_assert(!SelectPolicy::multicast && !SelectPolicy::w2_fast_repair &&
                  !SelectPolicy::w3_exact_target && !SelectPolicy::tcp_payload_layout &&
                  !SelectPolicy::allows_fallback);
    static_assert(!TcpPolicy::multicast && !TcpPolicy::w2_fast_repair &&
                  TcpPolicy::w3_exact_target && TcpPolicy::tcp_payload_layout &&
                  TcpPolicy::allows_fallback);
    static_assert(MulticastPolicy::multicast && MulticastPolicy::w2_fast_repair &&
                  !MulticastPolicy::w3_exact_target && !MulticastPolicy::tcp_payload_layout &&
                  !MulticastPolicy::allows_fallback);

    /* Every policy satisfies the registry concept. */
    static_assert(RoutePolicy<SelectPolicy> && RoutePolicy<TcpPolicy> &&
                  RoutePolicy<MulticastPolicy>);

    const profile::TargetProfile select_profile = profile_with(profile::kRouteSelectStack, 0);
    const profile::TargetProfile tcp_profile = profile_with(profile::kRouteTcpZerocopy, 0);
    const profile::TargetProfile mcast_profile = profile_with(profile::kRouteMulticastWaiter, 0);
    const profile::TargetProfile auto_profile = profile_with(profile::kRouteAuto, 0);

    assert(SelectPolicy::supported(select_profile) && !SelectPolicy::supported(tcp_profile));
    assert(TcpPolicy::supported(tcp_profile) && !TcpPolicy::supported(mcast_profile));
    assert(MulticastPolicy::supported(mcast_profile) && !MulticastPolicy::supported(select_profile));
    assert(!SelectPolicy::supported(auto_profile) && !TcpPolicy::supported(auto_profile) &&
           !MulticastPolicy::supported(auto_profile));

    /* make_route_policy resolves the supported policy (first match wins). */
    assert(std::holds_alternative<SelectPolicy>(make_route_policy(select_profile)));
    assert(std::holds_alternative<TcpPolicy>(make_route_policy(tcp_profile)));
    assert(std::holds_alternative<MulticastPolicy>(make_route_policy(mcast_profile)));

    /* Capability projection follows the resolved policy. */
    assert(!route_needs_ghost_disarm(select_profile) && !route_needs_ghost_disarm(tcp_profile) &&
           route_needs_ghost_disarm(mcast_profile));
    assert(!route_capability(select_profile,
                             [](auto policy) {
                                 return std::decay_t<decltype(policy)>::tcp_payload_layout;
                             }));
    assert(route_capability(tcp_profile, [](auto policy) {
        return std::decay_t<decltype(policy)>::tcp_payload_layout;
    }));
    assert(!route_capability(mcast_profile, [](auto policy) {
        return std::decay_t<decltype(policy)>::tcp_payload_layout;
    }));

    memory::WriteRequest request = {.mode = memory::WriteMode::Zero};

    /* ---- Dispatch: only the resolved route runs. ---- */
    reset();
    RouteRunResult result = run_route(select_profile, &request, 1);
    assert(result.status.code == ROUTE_OK && !result.fallback_used);
    assert(state.select_calls == 1 && state.tcp_calls == 0 && state.multicast_calls == 0);

    reset();
    result = run_route(tcp_profile, &request, 1);
    assert(result.status.code == ROUTE_OK && !result.fallback_used);
    assert(state.tcp_calls == 1 && state.select_calls == 0);

    reset();
    result = run_route(mcast_profile, &request, 1);
    assert(result.status.code == ROUTE_OK);
    assert(state.multicast_calls == 1 && state.select_calls == 0);

    /* Unsupported routes never dispatch. */
    reset();
    result = run_route(auto_profile, &request, 1);
    assert(result.status.code == ROUTE_UNSUPPORTED && !result.fallback_used);
    assert(state.select_calls == 0 && state.tcp_calls == 0 && state.multicast_calls == 0);

    /* ---- Fallback: only an allowed, clean failure may fall back. ---- */
    const profile::TargetProfile tcp_with_select = profile_with(profile::kRouteTcpZerocopy,
                                                                profile::kRouteSelectStack);
    state = StubState{};
    state.tcp_status = {.code = ROUTE_FALLBACK_SAFE, .userspace_clean = 1, .kernel_disarmed = 1};
    state.select_status = {.code = ROUTE_OK};
    result = run_route(tcp_with_select, &request, 1);
    assert(result.status.code == ROUTE_OK && result.fallback_used);
    assert(state.tcp_calls == 1 && state.select_calls == 1);

    /* allow_fallback=0 keeps the tcp failure. */
    reset();
    state.tcp_status = {.code = ROUTE_FALLBACK_SAFE, .userspace_clean = 1, .kernel_disarmed = 1};
    result = run_route(tcp_with_select, &request, 0);
    assert(result.status.code == ROUTE_FALLBACK_SAFE && !result.fallback_used);
    assert(state.tcp_calls == 1 && state.select_calls == 0);

    /* A dirty failure is never fallback-safe. */
    reset();
    state.tcp_status = {.code = ROUTE_DIRTY_FAILURE, .userspace_clean = 0, .kernel_disarmed = 1};
    result = run_route(tcp_with_select, &request, 1);
    assert(result.status.code == ROUTE_DIRTY_FAILURE && !result.fallback_used);
    assert(state.select_calls == 0);

    /* Select declares no fallback, so a clean select failure stays put. */
    const profile::TargetProfile select_with_fallback = profile_with(
        profile::kRouteSelectStack, profile::kRouteTcpZerocopy);
    reset();
    state.select_status = {.code = ROUTE_FALLBACK_SAFE, .userspace_clean = 1,
                           .kernel_disarmed = 1};
    result = run_route(select_with_fallback, &request, 1);
    assert(result.status.code == ROUTE_FALLBACK_SAFE && !result.fallback_used);
    assert(state.select_calls == 1 && state.tcp_calls == 0);

    /* A declared fallback equal to the primary route is ignored. */
    reset();
    state.tcp_status = {.code = ROUTE_FALLBACK_SAFE, .userspace_clean = 1, .kernel_disarmed = 1};
    result = run_route(profile_with(profile::kRouteTcpZerocopy, profile::kRouteTcpZerocopy),
                       &request, 1);
    assert(result.status.code == ROUTE_FALLBACK_SAFE && !result.fallback_used);
    assert(state.tcp_calls == 1);

    /* run_policy_by_kind maps wire kinds to the matching policy only. */
    reset();
    RouteStatus by_kind = run_policy_by_kind(RouteKind::MulticastWaiter, &request);
    assert(by_kind.code == ROUTE_OK && state.multicast_calls == 1);
    by_kind = run_policy_by_kind(RouteKind::Auto, &request);
    assert(by_kind.code == ROUTE_UNSUPPORTED);

    puts("route_policy_test: ok");
    return 0;
}
