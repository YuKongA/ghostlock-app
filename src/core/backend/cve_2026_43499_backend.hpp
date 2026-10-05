#ifndef GHOSTLOCK_CVE2026_43499_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43499_BACKEND_HPP

#include "backend/cve_2026_43499/backend_profile/model.hpp"
#include "contract/identity.hpp"
#include "session/core_session.hpp"
#include "contract/stage_result.hpp"
#include "terminal/rooted_child.hpp"

#include "backend/cve_2026_43499/primitives.hpp"
#include "backend/cve_2026_43499/steps.hpp"
#include "backend/cve_2026_43499_state.hpp"

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    using ghostlock::contract::StageResult;

    /* cve_2026_43499 backend: the setup stage (in the unit) plus one step set.
     * The step vocabulary is the template parameter (ADR-0004 R18): W1W3 and
     * W1W2 are two compile-time instances, and W1W2 never compiles W3. The
     * route stays backend-internal and is dispatched inside run(); the shared
     * write/zero primitives come from the non-template Cve43499Primitives base,
     * so no StepSet ever enters the "do_one_write" symbol.
     *
     * The template definition and the explicit instantiations live in the unit;
     * callers only include this header. */
    template <class StepSet>
    struct Cve2026_43499Backend : Cve43499Primitives {
        static constexpr contract::BackendKind kind = contract::BackendKind::Cve2026_43499;
        static constexpr contract::StepSetKind steps = StepSet::kind;

        /* B2 state contract (D3): the 43499 state unit owns the opaque
         * CoreSession slot; this policy only forwards. Pipeline's RAII guard
         * constructs it at run entry and destroys it on every exit path. */
        using State = Cve2026_43499State;

        static void state_construct(CoreSession &session) noexcept {
            cve43499_state_construct(session);
        }

        static void state_destroy(CoreSession &session) noexcept {
            cve43499_state_destroy(session);
        }

        /* Bind the neutral Document against this backend's owner Schema and
         * install the frozen TargetProfile into the CoreSession slot (outside
         * the PI window, before run). Returns the bind/copy status fail-closed. */
        [[nodiscard]] static profile::BindStatus state_from(
                CoreSession &session, const profile::Document &document);

        /* setup -> W1 -> W2 (-> W3), then hand the rooted child to the terminal
         * (see pipeline::Pipeline::run). Route comes from the installed profile
         * and is dispatched internally; on Continue 'out' receives the
         * transfer. */
        [[nodiscard]] static StageResult run(CoreSession &session,
                                             const char *debug_dir, bool force_attack,
                                             ghostlock::terminal::RootedChild &out);
    };

    /* The two catalogued step-set instances. */
    using Cve43499_W1W3 = Cve2026_43499Backend<W1W3Steps>;
    using Cve43499_W1W2 = Cve2026_43499Backend<W1W2Steps>;

    /* Both catalogued step-set instances satisfy the state contract; the
     * Pipeline RAII guard relies on it. */
    static_assert(contract::BackendState<Cve43499_W1W3>);
    static_assert(contract::BackendState<Cve43499_W1W2>);

    /* Back-compat alias: existing call sites name the app-descendant instance
     * (W1W3), which is the behaviour before T4. */
    using Cve2026_43499Policy = Cve43499_W1W3;

    /* Availability is owned by contract::backend_available(); the
     * execution policy carries only the id. The declared identity and this
     * policy must name the same backend. */
    static_assert(Cve2026_43499Policy::kind == contract::backend::Cve2026_43499::kind);
    static_assert(contract::backend_available(Cve2026_43499Policy::kind));
} // namespace ghostlock::backend

#endif
