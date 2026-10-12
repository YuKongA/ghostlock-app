#pragma once
/** @file backend/cve_2026_43499/steps/access_binder.hpp
 *  Binding a window stage to the access facade.
 *
 *  This is the one place that turns the facts a caller holds into the access the
 *  engine writes through: it builds the route's subclass, initializes it with that
 *  route's parameter set, and hands back the landing seam.  It is a primitive, not
 *  an upper layer -- deciding *when* to bind, and where the facts came from, belongs
 *  to the caller (in Lua, per the 2026-10-12 ruling).
 *
 *  The access is owned by @p owner, which the caller must keep alive for as long as
 *  the seam is used: the seam is a borrowed pointer into it.
 */
#include <memory>

#include "backend/cve_2026_43499/access_multicast_waiter.hpp"
#include "backend/cve_2026_43499/steps/window_runner.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    // TODO(routeexec-compile-time): 机械改动（类型形状）—— 本结构此前非模板，其 `exec` 是
    //   `seam::RouteExec *`（虚基类指针）；按用户裁定与 docs/analysis/43499-plan-routeexec-compile-time.md
    //   改以具体路由类型参数化（窗口对它的调用是编译期）。完成 = 三线门禁绿 ＋ 真机判据不变 ＋ 用户确认 ⇒ 删本标注。
    /** What binding needs. All three are borrowed except the target, which is copied.
     *  @tparam Route The route controller's concrete type, known at the call site. */
    template <seam::RouteExec Route>
    struct AccessBindInputs final {
        Route *exec = nullptr;
        const route::MulticastLimits *limits = nullptr;
        TargetState target{};
    };

    /** Bind the multicast route's access and hand back its landing seam.
     *  @param inputs The route controller, its limits and the facts a write lands on.
     *  @param owner Receives the access; must outlive every use of the returned seam.
     *  @param out Receives the landing seam, or null when binding did not happen.
     *  @return nullptr on success, otherwise a named reason -- the caller reports it as
     *          it stands, and the engine keeps its own route fallback rather than
     *          writing through a seam that was never bound. */
    /* The owner is a unique_ptr because the access is not copyable (its seam holds the
     *  window by reference); the caller keeps it alive, and the seam borrows from it. */
    template <seam::RouteExec Route>
    [[nodiscard]] inline const char *bind_multicast_access(const AccessBindInputs<Route> &inputs,
                                                           std::unique_ptr<MulticastWaiterAccess<Route>> &owner,
                                                           AccessSeam *&out) noexcept {
        out = nullptr;
        if (inputs.exec == nullptr || inputs.limits == nullptr) {
            return "access-bind-missing-route";
        }
        /* Order matters: the readiness answer names the earliest missing fact, so a
         *  caller that has not found the object yet is told that and not the page. */
        if (write_readiness(inputs.target) != WriteReadiness::Ready) {
            return "access-bind-facts-incomplete";
        }
        owner = std::make_unique<MulticastWaiterAccess<Route>>(*inputs.exec, *inputs.limits, inputs.target);
        Multicast43499Parameters params{};
        params.route = profile::RouteId::MulticastWaiter;
        params.offset = 0U;
        params.offset_set = true;
        if (!owner->initialize(params).ok()) {
            return "access-bind-initialize-refused";
        }
        out = owner->landing_seam();
        return out == nullptr ? "access-bind-no-seam" : nullptr;
    }

    /** Bind and hand the seam straight to the stage context, which is what the runner
     *  reads.  Kept here rather than at the call site so the decision -- and the reason
     *  when there is none -- lives with the backend instead of in the launcher.
     *  @param inputs Same as bind_multicast_access.
     *  @param owner Receives the access; must outlive the context's use of the seam.
     *  @note Lifetime rule, because the seam borrows rather than owns: the route the
     *        access was bound to lives one round, so @p owner must be released **before**
     *        that route dies -- declare it after the route, or reset it explicitly.
     *        That is the AGENTS §5 order: stop access, unhook, then reclaim. Reversing it
     *        leaves the seam pointing at a dead route.
     *  @param context Receives the landing seam when binding succeeded.
     *  @return nullptr on success, otherwise the same named reasons. */
    template <seam::RouteExec Route>
    [[nodiscard]] inline const char *bind_stage_landing(const AccessBindInputs<Route> &inputs,
                                                        std::unique_ptr<MulticastWaiterAccess<Route>> &owner,
                                                        WindowStageContext<Route> &context) noexcept {
        AccessSeam *seam = nullptr;
        const char *why = bind_multicast_access(inputs, owner, seam);
        if (why != nullptr) {
            return why;
        }
        context.seam_landing = seam;
        return nullptr;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
