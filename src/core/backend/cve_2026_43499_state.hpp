#ifndef GHOSTLOCK_CVE2026_43499_STATE_HPP
#define GHOSTLOCK_CVE2026_43499_STATE_HPP

#include "memory/address_space.h"
#include "memory/heap_context.h"
#include "profile/model.h"
#include "race/pi_race.h"
#include "session/core_session.hpp"
#include "backend/victim/victim_context.hpp"
#include "support/native_resource.hpp"

#include <cassert>
#include <cstddef>
#include <new>
#include <sys/types.h>

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    using ghostlock::session::kBackendStateAlign;
    using ghostlock::session::kBackendStateBytes;
    using ghostlock::backend::victim::VictimContext;
    /* 43499-private state, carved out of the shared session (ADR-0002).
 * Member order mirrors the pre-rewrite ExploitSession so absolute field
 * offsets are unchanged (Phase 0 plan layout evidence). Do not reorder. */
    struct Cve2026_43499State final {
        profile::TargetProfile profile{};
        memory::ResolvedAddresses addresses{};
        memory::HeapContext heap{};
        race::PiRace race{};
        VictimContext victim{};
        pid_t parked_victim = -1;
        support::UniqueFd parked_victim_cmd{};

        Cve2026_43499State() noexcept;

        Cve2026_43499State(const Cve2026_43499State &) = delete;

        Cve2026_43499State &operator=(const Cve2026_43499State &) = delete;
    };

    static_assert(sizeof(Cve2026_43499State) <= kBackendStateBytes,
                  "increase CoreSession::kBackendStateBytes");
    static_assert(alignof(Cve2026_43499State) <= kBackendStateAlign,
                  "increase CoreSession::kBackendStateAlign");

    /* Typed accessor: the only way attack/route/backend code reaches the state.
 * Precondition: cve43499_state_construct() ran (composition root, outside the
 * PI window). No runtime check here: an extra load/branch per access moves the
 * cmp_disasm attack functions (Phase 0 gate); the host layout test covers it. */
    inline Cve2026_43499State &cve43499_state(CoreSession &state) noexcept {
        return *std::launder(reinterpret_cast<Cve2026_43499State *>(state.backend_state));
    }

    inline const Cve2026_43499State &cve43499_state(const CoreSession &state) noexcept {
        return *std::launder(
                reinterpret_cast<const Cve2026_43499State *>(state.backend_state));
    }

    /* Idempotent in-place construction (outside the PI window). */
    void cve43499_state_construct(CoreSession &state) noexcept;
} // namespace ghostlock::backend

#endif
