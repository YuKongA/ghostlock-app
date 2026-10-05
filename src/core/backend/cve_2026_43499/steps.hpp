#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_STEPS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_STEPS_HPP

#include "contract/identity.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::session {
    struct CoreSession;
} // namespace ghostlock::session

namespace ghostlock::backend {
    /* Backend-private step vocabularies (ADR-0004 R18). Each is a compile-time
     * policy whose run<M> is the W1 -> W2 (-> W3) sequence for one route M.
     * Adding a step set is a new type here plus a catalog triple; the pipeline
     * never branches on the kind.
     *
     * W1W2 is the shell / kernel-spawned entry: there is no seccomp filter to
     * clear, so W3 is not part of this type at all (it is never instantiated for
     * a W1W2 route). W1W3 keeps the original app-descendant sequence. */
    struct W1W3Steps final {
        static constexpr contract::StepSetKind kind = contract::StepSetKind::W1W3;

        template <class M>
        [[nodiscard]] static session::StageResult run(session::CoreSession &session,
                                                      session::VictimChain &chain);
    };

    struct W1W2Steps final {
        static constexpr contract::StepSetKind kind = contract::StepSetKind::W1W2;

        template <class M>
        [[nodiscard]] static session::StageResult run(session::CoreSession &session,
                                                      session::VictimChain &chain);
    };
} // namespace ghostlock::backend

#endif
