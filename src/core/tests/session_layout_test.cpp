/* Locks the CoreSession layout against the pre-rewrite ExploitSession layout
 * (ADR-0002 / Phase 0). If this fails, attack-function field offsets moved. */
#include "session/core_session.hpp"
#include "backend/cve_2026_43499_state.hpp"

#include <cstddef>
#include <cstdio>
#include <sys/types.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

namespace {
    using Core = ghostlock::session::CoreSession;
    using State = ghostlock::backend::Cve2026_43499State;

    /* Test-only replica of the pre-rewrite ExploitSession field order. */
    struct LegacySession {
        ghostlock::config::RuntimeConfig runtime{};
        ghostlock::profile::TargetProfile profile{};
        ghostlock::memory::ResolvedAddresses addresses{};
        ghostlock::memory::HeapContext heap{};
        ghostlock::race::PiRace race{};
        ghostlock::backend::victim::VictimContext victim{};
        pid_t parked_victim = -1;
        ghostlock::support::UniqueFd parked_victim_cmd{};
    };

    constexpr std::size_t kBase = __builtin_offsetof(Core, backend_state);
    static_assert(kBase == __builtin_offsetof(LegacySession, profile),
                  "backend state must start where profile did");
    static_assert(sizeof(State) <= ghostlock::session::kBackendStateBytes,
                  "backend state does not fit the CoreSession slot");
    static_assert(alignof(State) <= ghostlock::session::kBackendStateAlign,
                  "backend state alignment exceeds the CoreSession slot");

#define LOCK(member)                                                                        \
    static_assert(kBase + __builtin_offsetof(State, member) ==                              \
                          __builtin_offsetof(LegacySession, member),                        \
                  "field offset moved: " #member)
    LOCK(profile);
    LOCK(addresses);
    LOCK(heap);
    LOCK(race);
    LOCK(victim);
    LOCK(parked_victim);
    LOCK(parked_victim_cmd);
#undef LOCK
} // namespace

int main() {
    std::printf("session_layout_test: base=%zu state=%zu ok\n", kBase, sizeof(State));
    return 0;
}
