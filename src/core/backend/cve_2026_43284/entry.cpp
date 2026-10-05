/* ADR-0006 T4: cve_2026_43284 entry seam implementation.
 *
 * The concrete backend headers are included here; main.cpp names only
 * entry.hpp. Every path forwards to the shipped implementation unchanged. */

#include "backend/cve_2026_43284/entry.hpp"

#include "backend/cve_2026_43284/diagnostic.hpp"
#include "session/core_session.hpp"

#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::entry {

    int run_diagnostic(std::string_view module_path) {
        return diagnostic::run_diagnostic_cli(module_path);
    }

    bool ProductionSession::read_side_channel(int fd) noexcept {
        return read_session_secret_frame(fd, &secrets_.value) ==
               SessionFrameStatus::Ok;
    }

    std::uint8_t ProductionSession::bind(session::CoreSession &session,
                                         const profile::Document &document,
                                         bool allow_dev_target) {
        cve_2026_43284_state_construct(session);
        const ExecutionBindResult result = bind_production_execution(
                session, production_, document, secrets_.value, allow_dev_target);
        return static_cast<std::uint8_t>(result.error);
    }

} // namespace ghostlock::backend::cve_2026_43284::entry
