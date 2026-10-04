#ifndef GHOSTLOCK_SUPPORT_CLI_HPP
#define GHOSTLOCK_SUPPORT_CLI_HPP

/* Process command-line parsing for the native ghostlock executable.
 *
 * The parser is split out of core/main.cpp so the flag surface (and its
 * mutual-exclusion rules) is host-testable without linking the attack runtime.
 * It performs no I/O and touches no global state.
 *
 * Entry-point modes are exclusive: exactly zero or one of
 * --ghostlock-app-call, --load-prebuilt-profile, --probe-cve-2026-43284 and
 * --run-cve-2026-43284 may be given. --force-attack / --dump-kernel-log /
 * --enable-status-record modify the app/prebuilt modes; the read-only
 * diagnostic and the staged runner reject them so neither can inherit a side
 * effect from the legacy switches. The dev-only --allow-dev-target is valid
 * only with --run-cve-2026-43284; without it the flag is a parse error. With
 * no diagnostic flag the existing behavior is preserved. */

#include <cstdint>

namespace ghostlock::support::cli {
    enum class Mode : std::uint8_t {
        None = 0,
        AppCall,
        PrebuiltFile,
        ProbeCve2026_43284,
        RunCve2026_43284,
    };

    /* Staged execution selector for --run-cve-2026-43284; the backend maps it
     * onto its own stage vocabulary. */
    enum class Cve43284Stage : std::uint8_t {
        Plan = 0,
        Write,
        Trigger,
        Full,
    };

    struct Options final {
        Mode mode = Mode::None;
        const char *load_prebuilt_profile = nullptr;
        const char *dump_kernel_log = nullptr;
        /* Non-null only for Mode::ProbeCve2026_43284. */
        const char *probe_module_path = nullptr;
        /* Non-null only for Mode::RunCve2026_43284. */
        const char *run_module_path = nullptr;
        const char *run_target_path = nullptr;
        Cve43284Stage run_stage = Cve43284Stage::Full;
        /* Dev-only staged-run switch: accept a non-vendor one-shot carrier
         * target (for example under /data/local/tmp). Valid only with
         * --run-cve-2026-43284; any other combination is a parse error. */
        bool allow_dev_target = false;
        bool force_attack = false;
        bool status_record = false;
    };

    enum class ParseError : std::uint8_t {
        None = 0,
        UnknownArgument,
        MissingArgument,
        MultipleEntrypoints,
        StatusRequiresAppCall,
        ProbeConflict,
        RunConflict,
        BadStage,
        StageRequiresRun,
        DevTargetRequiresRun,
    };

    /* Parses argv[1..argc). On success fills the output and returns true with
     * error == None. On failure the output is reset and error names the first
     * failing rule; the caller maps it to the existing stderr text. */
    [[nodiscard]] bool parse_arguments(int argc, char *const *argv, Options &out,
                                       ParseError &error) noexcept;
} // namespace ghostlock::support::cli

#endif
