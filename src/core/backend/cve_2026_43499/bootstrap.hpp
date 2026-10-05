#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_BOOTSTRAP_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_BOOTSTRAP_HPP

#include "backend/cve_2026_43499/backend_profile/model.hpp"

namespace ghostlock::backend {
    /* cve_2026_43499 bootstrap: validate/install the resolved profile into the
     * backend state, resolve its addresses and log the execution tuning. Called
     * from the backend's run_setup; kept out of `pipeline` so the backend does
     * not depend on the composition layer (ADR-0004 R1). */
    void log_execution_settings(const profile::kernel_offsets *profile);

    void resolve_profile_addresses();

    /* Validate the running kernel against the already-bound state profile,
     * publish the execution tuning and resolve the profile addresses. The
     * Document bind happened in the backend's state_from. */
    void install_profile();
} // namespace ghostlock::backend

#endif
