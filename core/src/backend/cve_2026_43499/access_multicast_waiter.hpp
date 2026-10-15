#pragma once
/** @file backend/cve_2026_43499/access_multicast_waiter.hpp
 *  The multicast-waiter route's kernel access.
 *
 *  One subclass per route (2026-10-12 ruling): the shared logic stays in
 *  KernelAccess43499 and this class adds only what the route alone knows --
 *  which parameter set initialize accepts, and which landing lands the word.
 *
 *  落地原语 wiring: MulticastLanding decides (route, target, offset) and hands the window
 *  work to a Window.  The window is borrowed and must outlive this access; a caller
 *  with no window gets the unnamed construction, and initialize then refuses by name,
 *  which is the same rule every door here follows.
 */
#include <optional>

#include "backend/cve_2026_43499/access_params.hpp"
#include "backend/cve_2026_43499/kernel_access.hpp"
#include "backend/cve_2026_43499/landing/multicast_landing.hpp"
#include "backend/cve_2026_43499/landing/multicast_window.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** The exec of an access that has no route: what the route entry hands out before a caller
     *  gives it one. Every ask is a refusal, and such an access refuses before any window can
     *  exist, so this type is never the thing that writes.
     *  @note It satisfies the window's shape on purpose: "no route" is a shape question with an
     *        honest answer, and naming it keeps the never-wired case from needing a second class
     *        with a second set of state answers. */
    struct NoRouteExec final {
        /** @return false: with no route there is nothing to execute. */
        [[nodiscard]] bool execute(std::uint64_t, std::uint64_t, std::uint64_t) noexcept { return false; }
        /** @return false: with no route there is nothing to arm. */
        [[nodiscard]] bool arm(std::int32_t) noexcept { return false; }
    };

    // TODO(routeexec-compile-time): 机械改动（类型形状）—— 本类此前非模板，构造取 `landing::RouteExec &`、
    //   成员 `std::optional<landing::MulticastWindow>`（窗口以虚分派调用它的控制器）。按用户裁定与计划
    //   docs/analysis/43499-plan-routeexec-compile-time.md，改以具体路由类型参数化（编译期调用）。
    //   完成 = 三线门禁绿 ＋ 真机判据不变 ＋ Lead/用户确认 ⇒ 删本标注。
    /** The route's access, over the route type it is wired to (or `NoRouteExec` when it is held
     *  unwired by the route entry).
     *  @tparam Exec The route controller; the window this access builds calls it statically. */
    template <class Exec>
    class MulticastWaiterAccess final : public KernelAccess43499 {
    public:
        static constexpr profile::RouteId kRoute = profile::RouteId::MulticastWaiter;

        /** An access with no window: usable for the state machine, refused for writes. */
        MulticastWaiterAccess() noexcept = default;

        /** An access wired to the route's window and to the facts a write lands on.
         *  @param window The PI-window body; borrowed, must outlive this access.
         *  @param target The page base and mm the route will write through; borrowed too. */
        MulticastWaiterAccess(landing::Window &window, const TargetState &target) noexcept
            : wired_(true) {
            ops_.emplace(window);
            adopt(&*ops_, &target);
        }

        /** An access wired to a route body and to the facts a write lands on.  The window is
         *  built here so the caller hands over the two things only it has -- the route it
         *  already configured and the limits from the profile -- and the warm-up order stays
         *  in one place instead of being repeated at every call site.
         *  @param exec The route controller; borrowed, must outlive this access.
         *  @param limits The arm sequence and holds; borrowed too.
         *  @param target The page base and mm the route writes through; borrowed. */
        MulticastWaiterAccess(Exec &exec, const route::MulticastLimits &limits,
                              const TargetState &target) noexcept
            : wired_(true) {
            window_.emplace(exec, limits);
            ops_.emplace(*window_);
            adopt(&*ops_, &target);
        }

        /** @return whether this instance has a window wired at all. */
        [[nodiscard]] bool has_window() const noexcept { return wired_; }

        /** The landing, so the step layer can hand the engine the one write path this access
         *  owns.  Null until a window is wired: the engine then falls back to the route and
         *  says so, rather than this access pretending it could land a word.
         *  @return The landing landing, or null when nothing was wired. */
        [[nodiscard]] WordLanding *landing_landing() noexcept { return ops_ ? &*ops_ : nullptr; }

        /** The route only accepts its own parameter set, and only when a window is wired.
         *  @param parameters Must be a Multicast43499Parameters whose route is this route.
         *  @return ok when the route is up; a named refusal otherwise. */
        [[nodiscard]] contract::KernelAccess::AccessResult<bool> initialize(
            const AccessMiscParameters &parameters) noexcept override {
            using BoolResult = contract::KernelAccess::AccessResult<bool>;
            const auto *mine = dynamic_cast<const Multicast43499Parameters *>(&parameters);
            if (mine == nullptr) {
                return BoolResult::failed(AccessStatus::BadParameters);
            }
            if (!wired_) {
                return BoolResult::failed(AccessStatus::Unsupported);
            }
            /* super: the shared preconditions and the phase transition. */
            const BoolResult base = KernelAccess43499::initialize(parameters);
            if (!base.ok()) {
                return base;
            }
            return BoolResult::ok(true);
        }

    private:
        /* Optional because the landing needs its window at construction, and an unnamed
         *  access is a legitimate state: it just cannot come up. */
        std::optional<landing::MulticastWindow<Exec>> window_;
        std::optional<landing::MulticastLanding> ops_;
        bool wired_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43499
