#ifndef GHOSTLOCK_CVE2026_43499_BACKEND_HPP
#define GHOSTLOCK_CVE2026_43499_BACKEND_HPP

#include "profile/model.h"
#include "route/component_catalog.hpp"
#include "route/exploit_procedure.hpp"

namespace ghostlock::session::backend {
    /* Batch 4 (D1=B slice 3a) cve_2026_43499 backend procedure. Only the setup
     * stage is extracted so far; W1/W2/W3 stay in ExploitProcedure because they
     * interleave with the route (middleware) hooks. Behavior, statement order
     * and log text are unchanged. */
    StageResult run_setup(ExploitSession &session, const profile::kernel_offsets &decoded,
                          const char *debug_dir, bool force_attack);

    struct Cve2026_43499Policy final {
        static constexpr runtime::BackendKind kind = runtime::BackendKind::Cve2026_43499;
        static constexpr bool available = true;
    };
} // namespace ghostlock::session::backend

#endif
