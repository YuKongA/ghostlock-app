#ifndef GHOSTLOCK_CORE_SESSION_HPP
#define GHOSTLOCK_CORE_SESSION_HPP

#include "contract/capabilities.hpp"
#include "session/runtime_config.h"

#include <cstddef>

namespace ghostlock::session {
    /* Process-level state container (ADR-0002). Framework fields stay neutral;
 * 43499-specific state lives in an opaque slot constructed by the backend
 * (backend/cve_2026_43499_state.hpp) and destroyed by the hook below.
 *
 * The slot is declared immediately after runtime so the backend fields keep
 * their pre-rewrite absolute offsets (ADR-0002 / Phase 0 plan): the old
 * ExploitSession put profile at offset 104, and this slot starts at 104 too.
 * Keep the slot before any new framework field, and add new fields after it. */
    inline constexpr std::size_t kBackendStateBytes = 2048;
    inline constexpr std::size_t kBackendStateAlign = 8;

    struct CoreSession final {
        ghostlock::config::RuntimeConfig runtime{};
        alignas(kBackendStateAlign) std::byte backend_state[kBackendStateBytes]{};
        bool backend_state_ready = false;
        void (*backend_state_dtor)(void *) noexcept = nullptr;

        /* Non-owning capability view (contract-design.md section 5). Appended
         * last so no pre-existing field offset moves (session_layout_test locks
         * those). The composition root fills this in with implementations whose
         * lifetime covers the chain; a call-block-local view (for example the
         * 43499 Tier 1 adapter built in steps.cpp) is passed directly and must
         * NOT be stored here, because its adapters die with the block. A null
         * member means "not provided" (R7: unsupported is an error, never 0). */
        contract::Capabilities capabilities{};

        CoreSession() noexcept;

        ~CoreSession() noexcept;

        CoreSession(const CoreSession &) = delete;

        CoreSession &operator=(const CoreSession &) = delete;
    };

    extern CoreSession g_exploit_session;
} // namespace ghostlock::session

#endif
