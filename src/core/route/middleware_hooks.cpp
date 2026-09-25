/*
 * GhostLock — middleware route hooks (Batch 4, D1=B slice 3c).
 *
 * Direct dispatch from the resolved route policy to its static hook: the
 * per-policy `if` chain is expanded at compile time by for_each_policy, so no
 * vtable or std::visit appears and the PI-window path stays indirect-call free.
 */

#include "route/middleware_hooks.hpp"

#include "route/route_policy.hpp"
#include "session/exploit_session.hpp"

namespace ghostlock::route::middleware {
    std::optional<Status> resident_write(session::ExploitSession &exploit_session,
                                         const memory::WriteRequest &request) {
        std::optional<Status> handled = std::nullopt;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(exploit_session.profile)) {
                handled = P::resident_write(exploit_session, request);
                matched = true;
            }
        });
        return handled;
    }

    bool w1_resident_repair(session::ExploitSession &exploit_session) {
        bool repaired = true;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(exploit_session.profile)) {
                repaired = P::w1_resident_repair(exploit_session);
                matched = true;
            }
        });
        return repaired;
    }

    bool w2_fast_repair_prebuild(session::ExploitSession &exploit_session) {
        bool ready = true;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(exploit_session.profile)) {
                ready = P::w2_fast_repair_prebuild(exploit_session);
                matched = true;
            }
        });
        return ready;
    }

    bool w2_fast_repair_activate(session::ExploitSession &exploit_session) {
        bool repaired = true;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(exploit_session.profile)) {
                repaired = P::w2_fast_repair_activate(exploit_session);
                matched = true;
            }
        });
        return repaired;
    }

    uint32_t w1_attempt_cap(const profile::TargetProfile &profile, uint32_t base) {
        uint32_t capped = base;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(profile)) {
                if constexpr (P::multicast) {
                    /* A non-resident multicast write is one-shot. */
                    capped = profile.multicast_resident() ? base : 1;
                }
                matched = true;
            }
        });
        return capped;
    }

    bool w3_exact_target(const profile::TargetProfile &profile) {
        bool exact = false;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(profile)) {
                if constexpr (P::w3_exact_target) {
                    exact = true;
                }
                matched = true;
            }
        });
        return exact;
    }

    bool needs_scratch_repair(const profile::TargetProfile &profile) {
        bool needed = false;
        bool matched = false;
        for_each_policy([&]<class P>() {
            if (!matched && P::supported(profile)) {
                if constexpr (P::multicast) {
                    needed = !profile.multicast_resident();
                }
                matched = true;
            }
        });
        return needed;
    }
} // namespace ghostlock::route::middleware
