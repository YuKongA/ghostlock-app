/* Host test for the route-policy registry: compile-time capabilities, policy
 * selection and direct dispatch. R6a removed the declared fallback: native runs
 * exactly the one resolved route, and a failure is returned unchanged.
 *
 * White box: capabilities are asserted per policy (including the inherited
 * RoutePolicyDefaults) and every dispatch path is counted through stubs. */

#include "backend/cve_2026_43499/route/route_policy.hpp"

#include <cassert>
#include <cstdio>
#include <variant>

using namespace ghostlock;

namespace {
    struct StubState {
        ghostlock::backend::cve_2026_43499::route::RouteStatus select_status = {.code = ghostlock::backend::cve_2026_43499::route::ROUTE_OK};
        ghostlock::backend::cve_2026_43499::route::RouteStatus tcp_status = {.code = ghostlock::backend::cve_2026_43499::route::ROUTE_OK};
        ghostlock::backend::cve_2026_43499::route::RouteStatus multicast_status = {.code = ghostlock::backend::cve_2026_43499::route::ROUTE_OK};
        int32_t select_calls = 0;
        int32_t tcp_calls = 0;
        int32_t multicast_calls = 0;
    };

    StubState state;

    void reset() {
        state = StubState{};
    }

    profile::TargetProfile profile_with(uint8_t route) {
        static profile::kernel_offsets values;
        values = {};
        values.route = route;
        return profile::TargetProfile::from(&values);
    }

    /* R6a compile-time guard: the fallback surface is gone. */
    template<class P>
    concept HasAllowsFallback = requires { P::allows_fallback; };

    template<class T>
    concept HasFallbackRoute = requires(const T &p) { p.fallback_route(); };
} // namespace

namespace ghostlock::backend::cve_2026_43499::route {
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
} // namespace ghostlock::backend::cve_2026_43499::route

int32_t main(void) {
    using namespace ghostlock::backend::cve_2026_43499::route;
    using ghostlock::profile::RouteKind;

    /* ---- White box: capabilities, including the base-class defaults. ---- */
    static_assert(SelectPolicy::kind == RouteKind::SelectStack);
    static_assert(TcpPolicy::kind == RouteKind::TcpZerocopy);
    static_assert(MulticastPolicy::kind == RouteKind::MulticastWaiter);

    static_assert(!SelectPolicy::multicast && !SelectPolicy::w2_fast_repair &&
                  !SelectPolicy::w3_exact_target && !SelectPolicy::tcp_payload_layout);
    static_assert(!TcpPolicy::multicast && !TcpPolicy::w2_fast_repair &&
                  TcpPolicy::w3_exact_target && TcpPolicy::tcp_payload_layout);
    static_assert(MulticastPolicy::multicast && MulticastPolicy::w2_fast_repair &&
                  !MulticastPolicy::w3_exact_target && !MulticastPolicy::tcp_payload_layout);

    /* R6a: no policy exposes allows_fallback and TargetProfile has no
     * fallback_route(); the fallback branch cannot come back silently. */
    static_assert(!HasAllowsFallback<RoutePolicyDefaults>);
    static_assert(!HasAllowsFallback<SelectPolicy>);
    static_assert(!HasAllowsFallback<TcpPolicy>);
    static_assert(!HasAllowsFallback<MulticastPolicy>);
    static_assert(!HasFallbackRoute<profile::TargetProfile>);

    /* Every policy satisfies the registry concept. */
    static_assert(RoutePolicy<SelectPolicy> && RoutePolicy<TcpPolicy> &&
                  RoutePolicy<MulticastPolicy>);

    const profile::TargetProfile select_profile = profile_with(profile::kRouteSelectStack);
    const profile::TargetProfile tcp_profile = profile_with(profile::kRouteTcpZerocopy);
    const profile::TargetProfile mcast_profile = profile_with(profile::kRouteMulticastWaiter);
    const profile::TargetProfile none_profile = profile_with(profile::kRouteNone);
    /* The deprecated legacy v1/v2 value 0 resolves no policy either. */
    const profile::TargetProfile legacy_auto_profile = profile_with(profile::kRouteAuto);

    assert(SelectPolicy::supported(select_profile) && !SelectPolicy::supported(tcp_profile));
    assert(TcpPolicy::supported(tcp_profile) && !TcpPolicy::supported(mcast_profile));
    assert(MulticastPolicy::supported(mcast_profile) && !MulticastPolicy::supported(select_profile));
    assert(!SelectPolicy::supported(none_profile) && !TcpPolicy::supported(none_profile) &&
           !MulticastPolicy::supported(none_profile));
    assert(!SelectPolicy::supported(legacy_auto_profile) &&
           !TcpPolicy::supported(legacy_auto_profile) &&
           !MulticastPolicy::supported(legacy_auto_profile));

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
    RouteStatus result = run_route(select_profile, &request);
    assert(result.code == ROUTE_OK);
    assert(state.select_calls == 1 && state.tcp_calls == 0 && state.multicast_calls == 0);

    reset();
    result = run_route(tcp_profile, &request);
    assert(result.code == ROUTE_OK);
    assert(state.tcp_calls == 1 && state.select_calls == 0);

    reset();
    result = run_route(mcast_profile, &request);
    assert(result.code == ROUTE_OK);
    assert(state.multicast_calls == 1 && state.select_calls == 0);

    /* Unsupported routes never dispatch. */
    reset();
    result = run_route(none_profile, &request);
    assert(result.code == ROUTE_UNSUPPORTED);
    assert(state.select_calls == 0 && state.tcp_calls == 0 && state.multicast_calls == 0);

    /* ---- R6a: a clean tcp failure is returned unchanged; no other route
     * runs, even though select geometry is present in the profile. ---- */
    reset();
    state.tcp_status = {.code = ROUTE_FALLBACK_SAFE, .userspace_clean = 1, .kernel_disarmed = 1};
    result = run_route(tcp_profile, &request);
    assert(result.code == ROUTE_FALLBACK_SAFE);
    assert(state.tcp_calls == 1 && state.select_calls == 0);

    /* A dirty failure is returned unchanged too. */
    reset();
    state.tcp_status = {.code = ROUTE_DIRTY_FAILURE, .userspace_clean = 0, .kernel_disarmed = 1};
    result = run_route(tcp_profile, &request);
    assert(result.code == ROUTE_DIRTY_FAILURE);
    assert(state.tcp_calls == 1 && state.select_calls == 0);

    /* run_policy_by_kind maps wire kinds to the matching policy only. */
    reset();
    RouteStatus by_kind = run_policy_by_kind(RouteKind::MulticastWaiter, &request);
    assert(by_kind.code == ROUTE_OK && state.multicast_calls == 1);
    by_kind = run_policy_by_kind(RouteKind::None, &request);
    assert(by_kind.code == ROUTE_UNSUPPORTED);
    by_kind = run_policy_by_kind(static_cast<RouteKind>(profile::kRouteAuto), &request);
    assert(by_kind.code == ROUTE_UNSUPPORTED);

    puts("route_policy_test: ok");
    return 0;
}
