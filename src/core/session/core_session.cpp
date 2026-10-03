#include "session/core_session.hpp"

namespace ghostlock::session {
    CoreSession::CoreSession() noexcept {
        runtime.main_cpu = 0;
        runtime.consumer_cpu = 1;
        runtime.home_dir = "/data/local/tmp";
        runtime.root_script_path = "/data/local/tmp/.ghostlock_root.sh";
        /* Backend state is constructed by the composition root (Phase 0/A:
 * run_orchestrated_pipeline); the slot is intentionally inert here. */
    }

    CoreSession::~CoreSession() noexcept {
        if (backend_state_ready && backend_state_dtor != nullptr) {
            backend_state_dtor(backend_state);
        }
    }

    CoreSession g_exploit_session;
} // namespace ghostlock::session

namespace ghostlock::config {
    RuntimeConfig &runtime_config_snapshot() noexcept {
        return ghostlock::session::g_exploit_session.runtime;
    }
} // namespace ghostlock::config
