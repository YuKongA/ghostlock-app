#ifndef GHOSTLOCK_CVE2026_43284_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43284_BACKEND_HPP

#include <string_view>

#include "backend/cve_2026_43284/backend_terminal.hpp"
#include "pipeline/backend_contract.hpp"
#include "pipeline/component_catalog.hpp"
#include "session/core_session.hpp"
#include "session/stage_types.hpp"
#include "terminal/terminal_input.hpp"

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    using ghostlock::session::StageResult;

    /* CVE-2026-43284 backend execution policy (B5-7/B5-8). It pairs with the
     * umh_forward terminal input and runs the B5-6 endgame through the state's
     * injected ops (see backend/cve_2026_43284/backend_terminal.hpp). B5-8
     * catalogues the {43284, PageCacheWrite, UmhForward} triple so Pipeline and
     * the orchestrator switch compile, but backend_available() stays false and
     * the orchestrator's selection_supported() gate rejects it, so the backend
     * never runs on a device until the B5-9 bindings and device gate exist.
     *
     * Availability is owned by component_catalog::backend_available(); this
     * type carries the stable id, the step set, the execution/state contracts
     * and the reason. 43284 never reuses a cve_2026_43499 slot or state. */
    struct Cve2026_43284Policy final {
        static constexpr pipeline::BackendKind kind =
                pipeline::BackendKind::Cve2026_43284;
        static constexpr pipeline::StepSetKind steps =
                pipeline::StepSetKind::PageCacheWrite;
        static constexpr std::string_view unavailable_reason =
                "cve_2026_43284 catalogued (B5-8); not device-verified until B5-9";

        using State = cve_2026_43284::Cve2026_43284State;

        static void state_construct(CoreSession &session) noexcept {
            cve_2026_43284::cve_2026_43284_state_construct(session);
        }

        static void state_destroy(CoreSession &session) noexcept {
            cve_2026_43284::cve_2026_43284_state_destroy(session);
        }

        /* The endgame is filled through the state; run() returns Continue only
         * when the LKM/UMH terminus was reached and `out` carries the handoff. */
        [[nodiscard]] static StageResult run(
                CoreSession &session, const profile::kernel_offsets &decoded,
                const char *debug_dir, bool force_attack,
                ghostlock::terminal::UmhForwardInput &out);
    };

    /* The execution/state contracts and the catalogued triple now exist; the
     * backend stays unavailable (fail-closed) until the B5-9 device gate. */
    static_assert(pipeline::BackendIdentity<Cve2026_43284Policy>);
    static_assert(pipeline::BackendExecution<Cve2026_43284Policy,
                                             ghostlock::terminal::UmhForwardInput>);
    static_assert(pipeline::BackendState<Cve2026_43284Policy>);
    static_assert(!pipeline::backend_available(Cve2026_43284Policy::kind));
    static_assert(Cve2026_43284Policy::kind == pipeline::backend::Cve2026_43284::kind);
} // namespace ghostlock::backend

#endif
