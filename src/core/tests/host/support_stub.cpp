#include "host_attack_script.hpp"

#include "backend/cve_2026_43499/spray.hpp"
#include "support/decls.hpp"

/* Host stubs for the side-effecting support primitives the attack data flow
 * calls. No allocation, no socket, no kernel page: a fake base is handed back
 * and the route stub ignores it. */
namespace ghostlock::support {
    void log_sync(void) {}

    void disable_rseq_for_thread(void) {}
} // namespace ghostlock::support

/* The 43499 prepare/spray primitives moved to the backend spray module (F15
 * ownership move); the host harness links this stub file instead of spray.cpp,
 * so the stubs keep the same signatures in the new namespace. */
namespace ghostlock::backend::cve_2026_43499::spray {
    void log_startup_context(void) {}

    void init_p0_profile(void) {}

    uintptr_t prepare_good_kernel_page(const ghostlock::memory::WriteRequest &request) {
        (void) request;
        return host::script().page_base;
    }

    void discard_prebuilt_page(void) {}

    int32_t quarantine_reclaim_sockets(void) { return 1; }

    void release_quarantined_reclaim_sockets(void) {}
} // namespace ghostlock::backend::cve_2026_43499::spray
