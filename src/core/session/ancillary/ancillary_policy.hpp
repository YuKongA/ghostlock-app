#ifndef GHOSTLOCK_ANCILLARY_POLICY_HPP
#define GHOSTLOCK_ANCILLARY_POLICY_HPP

#include <concepts>
#include <cstdint>

#include "profile/model.h"
#include "support/status.hpp"

namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::session::ancillary {
    /* Stable ancillary-behavior ids. Explicit numeric values; never rely on the
     * compiler's enum layout. VrGuard is the first and currently the only
     * behavior; a new behavior appends an id here and a policy to the registry. */
    enum class AncillaryKind : std::uint8_t {
        VrGuard = 1,
    };

    /* When the controller runs a behavior, relative to the backend attack steps:
     *   PreSpawn   - W1 done, before the victim is spawned;
     *   PostSpawn  - the rooted child exists (today's W2b);
     *   PreHandoff - before the frontend handoff. */
    enum class AncillaryStage : std::uint8_t {
        PreSpawn = 0,
        PostSpawn = 1,
        PreHandoff = 2,
    };

    /* The backend's kernel write, injected at the call site: zero one word at an
     * already-translated kernel address. A plain function pointer keeps the
     * attack path free of virtual dispatch, and naming the effect rather than
     * the middleware's request type keeps this header (and every behavior's
     * plan) host-compilable — the host test passes a stub, the device build
     * passes the middleware's write. The adapter binds the session global, so
     * adding it does not add a parameter-derived call site to `attack_write`
     * (the disassembly gate requires that function's code to stay put). */
    using AncillaryZeroFn = Status (*)(std::uintptr_t target, const char *desc);

    /* Capabilities a behavior needs from the backend: the write primitive and the
     * stage-R read-back. Injected at the call site so this header stays
     * host-compilable; `write` is null where the backend has none to offer (the
     * host test), and a behavior that needs it must fail safe. */
    struct AncillaryContext {
        bool write_available = false;
        bool read_available = false;
        AncillaryZeroFn write_zero = nullptr;
    };

    /* Neutral defaults so the controller can walk every registered behavior. A
     * behavior that is off reports false; an empty apply is a no-op success. */
    struct AncillaryPolicyDefaults {
        static bool enabled(const profile::TargetProfile &) noexcept {
            return false;
        }

        template <class Middleware>
        static Status apply(AncillaryStage, ExploitSession &, AncillaryContext &) noexcept {
            return true;
        }
    };

    /* Ancillary-behavior contract. `kind` names the behavior, `enabled` gates it
     * from the resolved profile, and `apply<Middleware>` is the stage entry. The
     * write primitive depends on the middleware, so apply is templated like the
     * backend's steps. */
    template <class P, class Middleware>
    concept AncillaryPolicyFor = requires(const profile::TargetProfile &profile,
                                          ExploitSession &session,
                                          AncillaryContext &context) {
        { P::kind } -> std::convertible_to<AncillaryKind>;
        { P::enabled(profile) } -> std::same_as<bool>;
        { P::template apply<Middleware>(AncillaryStage::PreSpawn, session, context) }
            -> std::same_as<Status>;
    };
} // namespace ghostlock::session::ancillary

#endif
