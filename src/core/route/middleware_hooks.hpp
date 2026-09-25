#ifndef GHOSTLOCK_MIDDLEWARE_HOOKS_HPP
#define GHOSTLOCK_MIDDLEWARE_HOOKS_HPP

#include <cstdint>
#include <optional>

#include "memory/payload_builder.h"
#include "profile/model.h"
#include "support/status.hpp"

namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::route::middleware {
    /* Middleware route hooks (Batch 4, D1=B slice 3c). One direct dispatch per
     * hook: the policy resolved from the profile runs its static hook, so no
     * vtable or indirect dispatch enters the path. Only side-effecting steps
     * are hooks; pure capability queries are projected below. */

    /* The side-effecting hooks are an explicit call boundary: keeping them out
     * of the caller (LTO would otherwise inline the whole route body into the
     * shared write primitive) preserves the reviewed shape of the attack
     * functions and keeps middleware edits from moving them. */
    [[nodiscard]] [[gnu::noinline]] std::optional<Status> resident_write(
        session::ExploitSession &exploit_session,
        const memory::WriteRequest &request);

    [[nodiscard]] [[gnu::noinline]] bool w1_resident_repair(
        session::ExploitSession &exploit_session);

    [[nodiscard]] [[gnu::noinline]] bool w2_fast_repair_prebuild(
        session::ExploitSession &exploit_session);

    [[nodiscard]] [[gnu::noinline]] bool w2_fast_repair_activate(
        session::ExploitSession &exploit_session);

    /* Pure capability projections over the resolved middleware policy. */
    [[nodiscard]] uint32_t w1_attempt_cap(
        const profile::TargetProfile &profile, uint32_t base);

    [[nodiscard]] bool w3_exact_target(const profile::TargetProfile &profile);

    /* True when the resolved middleware needs the private-scratch W1b repair
     * (multicast, non-resident). The repair body itself stays in the procedure
     * because it calls the shared write primitive. */
    [[nodiscard]] bool needs_scratch_repair(const profile::TargetProfile &profile);
} // namespace ghostlock::route::middleware

#endif
