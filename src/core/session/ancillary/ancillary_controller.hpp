#ifndef GHOSTLOCK_ANCILLARY_CONTROLLER_HPP
#define GHOSTLOCK_ANCILLARY_CONTROLLER_HPP

#include <tuple>
#include <utility>

#include "session/ancillary/ancillary_policy.hpp"
#include "session/ancillary/vr_guard.hpp"
#include "session/exploit_session.hpp"

namespace ghostlock::session::ancillary {
    /* The registry: every ancillary behavior, in evaluation order. Adding a
     * behavior appends one type here; the controller never branches on kind. */
    using AncillaryPolicyList = std::tuple<VrGuardPolicy>;

    template <class Fn, class... Ps>
    constexpr void for_each_ancillary_policy(Fn &&fn, std::tuple<Ps...> *) {
        (fn.template operator()<Ps>(), ...);
    }

    template <class Fn>
    constexpr void for_each_ancillary_policy(Fn &&fn) {
        for_each_ancillary_policy(std::forward<Fn>(fn),
                                  static_cast<AncillaryPolicyList *>(nullptr));
    }

    /* Invoke fn<P>() for every behavior the resolved profile enables. This is the
     * only gate: a behavior is off unless it says so. Host-compilable and
     * side-effect-free, so the host test can lock the selection. */
    template <class Fn>
    void for_each_enabled_ancillary_policy(const profile::TargetProfile &profile, Fn &&fn) {
        for_each_ancillary_policy([&]<class P>() {
            if (P::enabled(profile)) fn.template operator()<P>();
        });
    }

    /* The controller. The pipeline fixes the backend step order and the
     * middleware policy; the controller only dispatches the enabled behaviors at
     * the requested stage, and every call site passes the middleware so the
     * behavior's write primitive keeps the same instantiation. Header-only and
     * host-compilable; behavior bodies that need the middleware's write primitive
     * land in stage C. */
    template <class Middleware>
    struct AncillaryController final {
        static Status apply(AncillaryStage stage, ExploitSession &session,
                            AncillaryContext &context) {
            Status ok = true;
            for_each_enabled_ancillary_policy(
                session.profile, [&]<class P>() {
                    ok = P::template apply<Middleware>(stage, session, context) && ok;
                });
            return ok;
        }
    };
} // namespace ghostlock::session::ancillary

#endif
