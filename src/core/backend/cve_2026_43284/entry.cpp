/* ADR-0006 T4: cve_2026_43284 entry seam implementation.
 *
 * The concrete backend headers are included here; main.cpp names only
 * entry.hpp. Every path forwards to the shipped implementation unchanged. */

#include "backend/cve_2026_43284/entry.hpp"

#include "backend/cve_2026_43284/diag_line.hpp"
#include "backend/cve_2026_43284/diagnostic.hpp"
#include "session/core_session.hpp"

#include <cstdint>
#include <cstdio>

namespace ghostlock::backend::cve_2026_43284::entry {
    namespace {
        /* Display token for the session-frame outcomes (design A.3/L5). */
        [[nodiscard]] const char *frame_status_name(SessionFrameStatus status) noexcept {
            switch (status) {
            case SessionFrameStatus::Ok: return "Ok";
            case SessionFrameStatus::Missing: return "Missing";
            case SessionFrameStatus::Truncated: return "Truncated";
            case SessionFrameStatus::BadVersion: return "BadVersion";
            case SessionFrameStatus::BadKind: return "BadKind";
            case SessionFrameStatus::BadLength: return "BadLength";
            case SessionFrameStatus::BadReserved: return "BadReserved";
            case SessionFrameStatus::BadIcvLen: return "BadIcvLen";
            case SessionFrameStatus::BadTrailer: return "BadTrailer";
            case SessionFrameStatus::IoError: return "IoError";
            }
            return "Unknown";
        }

        void emit_line(DiagLine &line) noexcept {
            (void)std::fputs(line.c_str(), stderr);
            (void)std::fflush(stderr);
        }
    } // namespace

    int run_diagnostic(std::string_view module_path) {
        return diagnostic::run_diagnostic_cli(module_path);
    }

    bool ProductionSession::read_side_channel(int fd) noexcept {
        const SessionFrameStatus status =
                read_session_secret_frame(fd, &secrets_.value);
        /* L5: the frame fingerprint only -- SPI/algorithms/ports/length. The key
         * material never reaches the log (design A.5: "密钥零输出"). */
        DiagLine line("session");
        if (status == SessionFrameStatus::Ok) {
            line.n("result", "ok")
                    .x("spi", secrets_.value.spi)
                    .u("encap_port", secrets_.value.encap_port)
                    .u("sender_port", secrets_.value.sender_port)
                    .u("icv_len", secrets_.value.icv_len)
                    .u("frame_payload_bytes", kSessionSecretFramePayloadBytes)
                    .n("keys", "withheld");
        } else {
            line.n("result", "rejected")
                    .n("reason", frame_status_name(status))
                    .n("keys", "withheld");
        }
        emit_line(line);
        return status == SessionFrameStatus::Ok;
    }

    std::uint8_t ProductionSession::bind(session::CoreSession &session,
                                         const profile::Document &document,
                                         bool allow_dev_target,
                                         plugin::PluginHost *plugin_host) {
        cve_2026_43284_state_construct(session);
        const ExecutionBindResult result = bind_production_execution(
                session, production_, document, secrets_.value, allow_dev_target,
                plugin_host);
        return static_cast<std::uint8_t>(result.error);
    }

} // namespace ghostlock::backend::cve_2026_43284::entry
