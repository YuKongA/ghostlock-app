#ifndef GHOSTLOCK_ANCILLARY_POLICY_HPP
#define GHOSTLOCK_ANCILLARY_POLICY_HPP

#include <concepts>
#include <cstdint>

#include "support/status.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::ancillary {
    using ghostlock::session::CoreSession;

    /* When the controller runs a behavior, relative to the backend attack steps:
     *   PreSpawn   - W1 done, before the victim is spawned;
     *   PostSpawn  - the rooted child exists (today's W2b);
     *   PreHandoff - before the terminal handoff. */
    enum class AncillaryStage : std::uint8_t {
        PreSpawn = 0,
        PostSpawn = 1,
        PreHandoff = 2,
    };

    /* The backend's kernel write, injected at the call site: zero one word at an
     * already-translated kernel address. A plain function pointer keeps the
     * attack path free of virtual dispatch, and naming the effect rather than
     * the middleware's request type keeps this header (and every behavior's
     * plan) host-compilable - the host test passes a stub, the device build
     * passes the middleware's write. The adapter binds the session global, so
     * adding it does not add a parameter-derived call site to `attack_write`
     * (the disassembly gate requires that function's code to stay put). */
    using AncillaryZeroFn = Status (*)(std::uintptr_t target, const char *desc);

    /* Caller-injected image-address -> direct-map translation. Same reasoning as
     * `AncillaryZeroFn`: a plain function pointer, no backend or middleware type
     * named, so a behavior with an image-relative target can resolve it without
     * this header ever seeing a backend address space. */
    using AncillaryAliasFn = std::uintptr_t (*)(std::uintptr_t image_addr);

    /* Per-invocation data the caller threads to the behaviors: availability of
     * the write/read capabilities, the injected write/alias handles, and the
     * rooted child's task at the PostSpawn call site. Neutral in that it names
     * no backend, profile or middleware type; the caller fills it in. (ADR-0004
     * R4 retires the old `AncillaryContext` in favour of
     * `contract::Capabilities`; this shape is the interim hand-rolled handle.) */
    struct AncillaryOps {
        bool write_available = false;
        bool read_available = false;
        AncillaryZeroFn write_zero = nullptr;
        AncillaryAliasFn image_to_direct_map = nullptr;
        /* The rooted child's `struct task`, set at the PostSpawn call site.
         * Behaviors that strip per-task state address it relative to this. */
        std::uintptr_t child_task = 0;
    };

    /* Neutral defaults so a behavior can opt into the no-op path. The gate is
     * the caller's now (the controller consults an injected functor), so the
     * mechanism carries no `enabled(view)` here. */
    struct AncillaryPolicyDefaults {
        template <class Middleware, class Context>
        static Status apply(AncillaryStage, CoreSession &, AncillaryOps &,
                            const Context &) noexcept {
            return true;
        }
    };

    /* Ancillary-behavior contract: one stage entry, templated on the backend
     * middleware (so a behavior can reach that middleware's primitives) and on
     * the caller-supplied Context (the neutral view the behavior reads). The
     * contract deliberately says nothing about how a behavior is gated: the
     * caller injects both the registry (a tuple) and the gate. */
    template <class P, class Middleware, class Context>
    concept AncillaryPolicyFor =
        requires(CoreSession &session, AncillaryOps &ops, const Context &context) {
            { P::template apply<Middleware>(AncillaryStage::PreSpawn, session, ops,
                                            context) } -> std::same_as<Status>;
        };
} // namespace ghostlock::ancillary

#endif
