#ifndef GHOSTLOCK_ORCHESTRATOR_HPP
#define GHOSTLOCK_ORCHESTRATOR_HPP

#include <memory>

#include "route/backend_policy.hpp"
#include "route/component_catalog.hpp"
#include "route/exploit_procedure.hpp"
#include "route/frontend_contract.hpp"
#include "route/pipeline.hpp"
#include "session/exploit_session.hpp"

namespace ghostlock::runtime {
    /* NativeOrchestrator (Batch 3): validate the component selection outside the
     * sensitive window and hand the chosen middleware off to the shared
     * ExploitProcedure pipeline. It owns selection/composition, never the
     * attack sequence, and adds no indirect dispatch to the PI window. */
    [[nodiscard]] inline std::unique_ptr<session::ExploitProcedure>
    make_orchestrated_procedure(session::ExploitSession &session,
                                const ComponentSelection &selection) {
        if (!selection_supported(selection)) return nullptr;
        return session::make_exploit_procedure(session, selection.middleware);
    }
} // namespace ghostlock::runtime

#endif
