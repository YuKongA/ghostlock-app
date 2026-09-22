#ifndef GHOSTLOCK_ROUTE_POLICY_HPP
#define GHOSTLOCK_ROUTE_POLICY_HPP

#include <concepts>
#include <tuple>
#include <type_traits>
#include <variant>

#include "memory/payload_builder.h"
#include "profile/model.h"
#include "route/route_status.h"

namespace ghostlock::route {
    RouteStatus do_pselect_fake_lock_route(const memory::WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const memory::WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request);

    using RouteKind = ghostlock::profile::RouteKind;

    /* One resolved route's behavior. A policy is a tag type whose compile-time
     * capabilities replace every `supports(RouteKind)` branch in the run stages,
     * and whose run() is the per-write entry point. Adding a route means adding
     * a policy and listing it in RoutePolicyList below; no stage edits. */
    template<class P>
    concept RoutePolicy = requires(const profile::TargetProfile &profile,
                                   const memory::WriteRequest *request)
    {
        { P::kind } -> std::convertible_to<RouteKind>;
        { P::supported(profile) } -> std::same_as<bool>;
        { P::run(request) } -> std::same_as<RouteStatus>;
    };

    struct SelectPolicy {
        static constexpr RouteKind kind = RouteKind::SelectStack;
        static constexpr bool multicast = false;
        static constexpr bool w2_fast_repair = false;
        static constexpr bool w3_exact_target = false;
        static constexpr bool tcp_payload_layout = false;
        static constexpr bool allows_fallback = false;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_pselect_fake_lock_route(request);
        }
    };

    struct TcpPolicy {
        static constexpr RouteKind kind = RouteKind::TcpZerocopy;
        static constexpr bool multicast = false;
        static constexpr bool w2_fast_repair = false;
        static constexpr bool w3_exact_target = true;
        static constexpr bool tcp_payload_layout = true;
        static constexpr bool allows_fallback = true;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_tcp_fake_lock_route(request);
        }
    };

    struct MulticastPolicy {
        static constexpr RouteKind kind = RouteKind::MulticastWaiter;
        static constexpr bool multicast = true;
        static constexpr bool w2_fast_repair = true;
        static constexpr bool w3_exact_target = false;
        static constexpr bool tcp_payload_layout = false;
        static constexpr bool allows_fallback = false;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_kernel5_fake_lock_route(request);
        }
    };

    /* The single registry. Appending a policy here wires every generic loop
     * below (variant, selection, fallback lookup, capabilities). */
    using RoutePolicyList = std::tuple<SelectPolicy, TcpPolicy, MulticastPolicy>;

    template<class T>
    struct variant_of;
    template<class... Ps>
    struct variant_of<std::tuple<Ps...>> {
        using type = std::variant<Ps...>;
    };

    template<class Fn, class... Ps>
    constexpr void for_each_policy(Fn &&fn, std::tuple<Ps...> *) {
        (fn.template operator()<Ps>(), ...);
    }

    template<class Fn>
    constexpr void for_each_policy(Fn &&fn) {
        for_each_policy(std::forward<Fn>(fn),
                        static_cast<RoutePolicyList *>(nullptr));
    }

    /* Derived from RoutePolicyList, so a new route touches one list only. */
    using RoutePolicyVariant = typename variant_of<RoutePolicyList>::type;

    /* Single route->policy mapping; for_each_policy keeps it automatic when the
     * registry grows. */
    [[nodiscard]] inline RoutePolicyVariant make_route_policy(
        const profile::TargetProfile &profile) noexcept {
        RoutePolicyVariant selected = SelectPolicy{};
        for_each_policy([&]<class P>() {
            if (P::supported(profile)) selected = P{};
        });
        return selected;
    }

    /* Visit the resolved policy so a call site can branch on compile-time
     * capabilities with `if constexpr` instead of on the route kind. Used by
     * the stage hooks, which run outside the PI window. */
    template<class Fn>
    constexpr decltype(auto) with_route_policy(
        const profile::TargetProfile &profile, Fn &&fn) {
        return std::visit(std::forward<Fn>(fn), make_route_policy(profile));
    }

    /* Read one compile-time capability of the resolved policy using a direct
     * compare/branch chain (no std::visit), safe for the PI window where an
     * indirect dispatch must not enter (route_lifecycle.hpp). */
    template<class Projection>
    bool route_capability(const profile::TargetProfile &profile,
                          Projection projection) noexcept {
        bool result = false;
        for_each_policy([&]<class P>() {
            if (!result && P::supported(profile)) result = projection(P{});
        });
        return result;
    }

    template<class P>
    bool run_policy_matching(RouteStatus &status, RouteKind kind,
                             const memory::WriteRequest *request) {
        if (kind != P::kind) return false;
        status = P::run(request);
        return true;
    }

    [[nodiscard]] inline RouteStatus run_policy_by_kind(
        RouteKind kind, const memory::WriteRequest *request) {
        RouteStatus status{.code = ROUTE_UNSUPPORTED};
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched) matched = run_policy_matching<P>(status, kind, request);
        });
        return status;
    }

    struct RouteRunResult {
        RouteStatus status{};
        bool fallback_used = false;
    };

    template<RoutePolicy Policy>
    RouteRunResult run_route_policy(const profile::TargetProfile &profile,
                                    const memory::WriteRequest *request,
                                    int32_t allow_fallback) {
        RouteRunResult result{};
        if (!Policy::supported(profile)) {
            result.status = RouteStatus{.code = ROUTE_UNSUPPORTED};
            return result;
        }
        result.status = Policy::run(request);
        if constexpr (Policy::allows_fallback) {
            if (result.status.code != ROUTE_OK && allow_fallback &&
                result.status.can_fallback()) {
                const RouteKind fallback = profile.fallback_route();
                if (fallback != RouteKind::Auto && fallback != Policy::kind) {
                    const RouteStatus next = run_policy_by_kind(fallback, request);
                    if (next.code != ROUTE_UNSUPPORTED) {
                        result.status = next;
                        result.fallback_used = true;
                    }
                }
            }
        }
        return result;
    }

    /* Select and run the resolved policy with a direct dispatch chain (no
     * std::visit), for the PI-window caller. */
    [[nodiscard]] inline RouteRunResult run_route(
        const profile::TargetProfile &profile,
        const memory::WriteRequest *request, int32_t allow_fallback) {
        RouteRunResult result{};
        bool handled = false;
        for_each_policy([&]<class P>() {
            if (!handled && P::supported(profile)) {
                result = run_route_policy<P>(profile, request, allow_fallback);
                handled = true;
            }
        });
        if (!handled) result.status = RouteStatus{.code = ROUTE_UNSUPPORTED};
        return result;
    }

    /* True when the resolved policy needs the post-race ghost disarm. */
    [[nodiscard]] inline bool route_needs_ghost_disarm(
        const profile::TargetProfile &profile) noexcept {
        return route_capability(profile, [](auto policy) {
            return std::decay_t<decltype(policy)>::multicast;
        });
    }
} // namespace ghostlock::route

#endif
