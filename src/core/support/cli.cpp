/* Command-line parsing for the native ghostlock executable.
 *
 * Pure: no allocation, no I/O, no global state. See support/cli.hpp for the
 * mode/conflict contract (S4 R2b: transport / run control / safety /
 * observability only; selection and policy come from the GLKv3 document). */

#include "support/cli.hpp"

#include <string_view>

namespace ghostlock::support::cli {
    bool parse_arguments(int argc, char *const *argv, Options &out,
                         ParseError &error) noexcept {
        out = Options{};
        error = ParseError::None;
        bool app_call = false;
        bool have_prebuilt = false;
        bool have_probe = false;
        bool have_plugin_probe = false;
        for (int index = 1; index < argc; ++index) {
            const std::string_view arg(argv[index]);
            if (arg == "--ghostlock-app-call") {
                app_call = true;
            } else if (arg == "--force-attack") {
                out.force_attack = true;
            } else if (arg == "--enable-status-record") {
                out.status_record = true;
            } else if (arg == "--allow-dev-target") {
                out.allow_dev_target = true;
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
                have_probe = true;            } else if (arg == "--plugin-probe") {
                if (index + 1 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.plugin_probe_path = argv[++index];
                have_plugin_probe = true;
            } else if (arg == "--expect-sha256") {
                if (index + 1 >= argc) {
                    error = ParseError::MissingArgument;
                    return false;
                }
                out.expect_sha256 = argv[++index];
            } else {
                error = ParseError::UnknownArgument;
                return false;
            }
        }

        const int entrypoints = (app_call ? 1 : 0) + (have_prebuilt ? 1 : 0) +
                                (have_probe ? 1 : 0) + (have_plugin_probe ? 1 : 0);
        if (entrypoints > 1) {
            error = ParseError::MultipleEntrypoints;
            return false;
        }
        if (have_probe) {
            /* The diagnostic is read-only: no attack switch, no log dump, no
             * status-record ACK channel and no carrier relaxation. Reject the
             * combination outright. */
            if (out.force_attack || out.dump_kernel_log != nullptr ||
                out.status_record || out.allow_dev_target) {
                error = ParseError::ProbeConflict;
                return false;
            }
            out.mode = Mode::ProbeCve2026_43284;
            return true;
        }
        if (have_plugin_probe) {
            /* The plugin probe is read-only too: no attack switch, no log dump,
             * no status-record channel and no carrier relaxation. */
            if (out.force_attack || out.dump_kernel_log != nullptr ||
                out.status_record || out.allow_dev_target) {
                error = ParseError::PluginProbeConflict;
                return false;
            }
            out.mode = Mode::PluginProbe;
            return true;
        }
        if (out.expect_sha256 != nullptr) {
            error = ParseError::ExpectShaRequiresPluginProbe;
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
