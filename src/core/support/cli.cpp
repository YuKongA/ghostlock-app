/* Command-line parsing for the native ghostlock executable.
 *
 * Pure: no allocation, no I/O, no global state. See support/cli.hpp for the
 * mode/conflict contract. */

#include "support/cli.hpp"

#include <string_view>

namespace ghostlock::support::cli {
    namespace {
        bool parse_stage(std::string_view text, Cve43284Stage &out) noexcept {
            if (text == "plan") {
                out = Cve43284Stage::Plan;
            } else if (text == "write") {
                out = Cve43284Stage::Write;
            } else if (text == "trigger") {
                out = Cve43284Stage::Trigger;
            } else if (text == "full") {
                out = Cve43284Stage::Full;
            } else {
                return false;
            }
            return true;
        }

        bool parse_hook_guard(std::string_view text,
                              Cve43284HookGuard &out) noexcept {
            if (text == "reject") {
                out = Cve43284HookGuard::Reject;
            } else if (text == "skip") {
                out = Cve43284HookGuard::Skip;
            } else {
                return false;
            }
            return true;
        }

        /* Consumes the required value of a --cve43284-* asset selector. */
        bool take_value(int argc, char *const *argv, int &index,
                        const char *&out) noexcept {
            if (index + 1 >= argc) {
                return false;
            }
            out = argv[++index];
            return true;
        }
    } // namespace

    bool parse_arguments(int argc, char *const *argv, Options &out,
                         ParseError &error) noexcept {
        out = Options{};
        error = ParseError::None;
        bool app_call = false;
        bool have_prebuilt = false;
        bool have_probe = false;
        bool have_run = false;
        bool have_cve43284_option = false;
        bool have_plugin = false;
        bool stage_set = false;
        const char *stage_text = nullptr;
        for (int index = 1; index < argc; ++index) {
            const std::string_view arg(argv[index]);
            if (arg == "--ghostlock-app-call") {
                app_call = true;
            } else if (arg == "--force-attack") {
                out.force_attack = true;
            } else if (arg == "--enable-status-record") {
                out.status_record = true;
            } else if (arg == "--load-prebuilt-profile") {
                if (index + 1 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.load_prebuilt_profile = argv[++index];
                have_prebuilt = true;
            } else if (arg == "--dump-kernel-log") {
                if (index + 1 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.dump_kernel_log = argv[++index];
            } else if (arg == "--probe-cve-2026-43284") {
                if (index + 1 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.probe_module_path = argv[++index];
                have_probe = true;
            } else if (arg == "--run-cve-2026-43284") {
                if (index + 2 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.run_module_path = argv[++index];
                out.run_target_path = argv[++index];
                have_run = true;
            } else if (arg == "--plugin") {
                if (!take_value(argc, argv, index, out.run_plugin_path)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                have_plugin = true;
            } else if (arg == "--allow-dev-target") {
                out.allow_dev_target = true;
            } else if (arg == "--cve43284-hook-target") {
                if (!take_value(argc, argv, index, out.run_hook_target)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                have_cve43284_option = true;
            } else if (arg == "--cve43284-hook-symbol") {
                if (!take_value(argc, argv, index, out.run_hook_symbol)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                have_cve43284_option = true;
            } else if (arg == "--cve43284-hook-guard") {
                const char *value = nullptr;
                if (!take_value(argc, argv, index, value)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                if (!parse_hook_guard(value, out.run_hook_guard)) {
                    error = ParseError::BadHookGuard;
                    return false;
                }
                have_cve43284_option = true;
            } else if (arg == "--cve43284-carrier") {
                if (!take_value(argc, argv, index, out.run_carrier_path)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                have_cve43284_option = true;
            } else if (arg == "--cve43284-patch1-target") {
                if (!take_value(argc, argv, index, out.run_patch1_target)) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                have_cve43284_option = true;
            } else if (arg == "--cve43284-allow-vermagic-rewrite") {
                out.allow_vermagic_rewrite = true;
                have_cve43284_option = true;
            } else if (arg == "--stage") {
                error = ParseError::MissingArgument;
                return false;
            } else if (arg.substr(0U, 8U) == "--stage=") {
                stage_text = argv[index] + 8;
                stage_set = true;
            } else {
                error = ParseError::UnknownArgument;
                return false;
            }
        }

        if (stage_set && !parse_stage(stage_text, out.run_stage)) {
            error = ParseError::BadStage;
            return false;
        }

        const int entrypoints = (app_call ? 1 : 0) + (have_prebuilt ? 1 : 0) +
                                (have_probe ? 1 : 0) + (have_run ? 1 : 0);
        if (entrypoints > 1) {
            error = ParseError::MultipleEntrypoints;
            return false;
        }
        if (have_probe) {
            /* The diagnostic is read-only: no attack switch, no log dump and no
             * status-record ACK channel. Reject the combination outright. */
            if (out.force_attack || out.dump_kernel_log != nullptr ||
                out.status_record || out.allow_dev_target ||
                have_cve43284_option || have_plugin) {
                error = ParseError::ProbeConflict;
                return false;
            }
            if (stage_set) {
                error = ParseError::StageRequiresRun;
                return false;
            }
            out.mode = Mode::ProbeCve2026_43284;
            return true;
        }
        if (have_run) {
            /* The staged runner prints its own structured records and reads its
             * own session frame; the legacy switches must not attach to it. */
            if (out.force_attack || out.dump_kernel_log != nullptr || out.status_record) {
                error = ParseError::RunConflict;
                return false;
            }
            out.mode = Mode::RunCve2026_43284;
            return true;
        }
        if (have_plugin) {
            error = ParseError::PluginRequiresRun;
            return false;
        }
        if (have_cve43284_option) {
            error = ParseError::Cve43284OptionRequiresRun;
            return false;
        }
        if (out.allow_dev_target) {
            error = ParseError::DevTargetRequiresRun;
            return false;
        }
        if (stage_set) {
            error = ParseError::StageRequiresRun;
            return false;
        }
        if (out.status_record && !app_call) {
            error = ParseError::StatusRequiresAppCall;
            return false;
        }
        if (app_call) {
            out.mode = Mode::AppCall;
        } else if (have_prebuilt) {
            out.mode = Mode::PrebuiltFile;
        } else {
            out.mode = Mode::None;
        }
        return true;
    }
} // namespace ghostlock::support::cli
