#pragma once
/** @file backend/cve_2026_43499/route/policy_dispatch.hpp
 *  Choosing a policy and running it, over values rather than over a profile.
 *
 *  The retired tree had this loop inline in its policy header, taking a TargetProfile by reference.
 *  That reference is the thing ADR-0015 removes: the execution layer decides nothing from the
 *  document, it is handed what it needs. So the shape is kept -- a compile-time chain, no visitor,
 *  first match wins, and an unsupported answer when nothing matches -- and the parameter is a caller
 *  supplied value type instead of the profile the old signature read.
 *
 *  This is deliberately the smallest piece that can exist on its own: it needs no route body, no
 *  race, and no system calls, so it can be finished and verified before any of the attack path is
 *  ported. A policy is a tag type with a static answer and a static run, exactly as the existing
 *  defaults header describes them.
 */
#include <concepts>
#include <cstddef>
#include <utility>

#include "backend/cve_2026_43499/route/route_status.hpp"

namespace ghostlock::backend::cve_2026_43499::route {
    /** What a policy must offer to take part: a static decision over the values, and a static run. */
    template <class Policy, class Values, class Request>
    concept RunnablePolicy = requires(const Values &values, const Request *request) {
        { Policy::supported(values) } -> std::same_as<bool>;
        { Policy::run(request) } -> std::same_as<RouteStatus>;
    };

    namespace detail {
        /** One step of the chain: this policy is asked only if no earlier one has answered. */
        template <class Policy, class Values, class Request>
        inline void dispatch_step(const Values &values, const Request *request, RouteStatus &status,
                                  bool &handled) {
            if (handled || !Policy::supported(values)) {
                return;
            }
            status = Policy::run(request);
            handled = true;
        }
    }  // namespace detail

    /**
     *  Run the first policy that says it applies, and report unsupported when none does.
     *
     *  @tparam Policies The policies in the order they must be tried. Each is checked against the
     *                   concept above, so one that cannot answer from the values stops the build.
     *  @param values    What the policies are allowed to decide from. Never a profile.
     *  @param request   The write being attempted; passed through untouched.
     *  @return The first applying policy's own report, or an unsupported report when none applies.
     *  @note  A policy that applies stops the chain: later policies are not asked, so that two policies
     *         claiming the same device cannot both act on one write. That is asserted rather than
     *         assumed, because it is the difference between one write and two.
     */
    template <class... Policies, class Values, class Request>
    [[nodiscard]] inline RouteStatus dispatch_policy(const Values &values, const Request *request) {
        static_assert((RunnablePolicy<Policies, Values, Request> && ...),
                      "every policy must decide from the values and run the request");
        /* The new tree names this RouteResultCode::Unsupported; the retired tree used a macro-style
         *  spelling, and the port keeps the value while taking the current name. */
        RouteStatus status{.code = RouteResultCode::Unsupported};
        bool handled = false;
        (detail::dispatch_step<Policies>(values, request, status, handled), ...);
        return status;
    }
}  // namespace ghostlock::backend::cve_2026_43499::route
