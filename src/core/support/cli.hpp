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
 * no diagnostic flag the existing behavior is preserved.
 *
 * B5-9h-1 adds the staged-hook asset selectors, all valid only with
 * --run-cve-2026-43284: --cve43284-hook-target <path> (default
 * /system/lib64/libc++.so), --cve43284-hook-symbol <mangled> (default the
 * libc++ ostream sentry C1 constructor), --cve43284-hook-guard reject|skip
 * (default skip), --cve43284-carrier <path> (default: the positional target)
 * and --cve43284-patch1-target <path> (default
 * /apex/com.android.runtime/bin/crash_dump64). Without the staged entry point
 * any of them is a parse error.
 *
 * delta-4 adds --plugin <path>, the dev/gate-only countermeasure loader. It is
 * a staged-runner-only escape hatch: legal only together with
 * --run-cve-2026-43284, rejected for every other mode, never read from a
 * profile and never reachable from the production app-call path. */

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

    /* Staged libc++ hook guard policy for --cve43284-hook-guard; the backend
     * maps it onto steps::HookGuardPolicy. Skip is the default because the
     * device's libc++.so has a BTI/PACIASP guard on the sentry entry and the
     * upstream rule advances +4 over it. */
    enum class Cve43284HookGuard : std::uint8_t {
        Reject = 0,
        Skip,
    };

    struct Options final {
        /* Pointers first: this keeps Options at one byte of tail padding, which
         * clang-tidy's performance.Padding check enforces. */
        const char *load_prebuilt_profile = nullptr;
        const char *dump_kernel_log = nullptr;
        /* Non-null only for Mode::ProbeCve2026_43284. */
        const char *probe_module_path = nullptr;
        /* Non-null only for Mode::RunCve2026_43284. */
        const char *run_module_path = nullptr;
        const char *run_target_path = nullptr;
        /* B5-9h-1 staged-hook assets. All are null/unset by default and are
         * only legal with --run-cve-2026-43284. A null path keeps the
         * documented default at the call site. */
        const char *run_hook_target = nullptr;
        const char *run_hook_symbol = nullptr;
        const char *run_carrier_path = nullptr;
        const char *run_patch1_target = nullptr;
        /* delta-4 dev/gate-only: countermeasure .so loaded through
         * plugin/loader and attached to the LKM window. Null by default. */
        const char *run_plugin_path = nullptr;
        Mode mode = Mode::None;
        Cve43284Stage run_stage = Cve43284Stage::Full;
        /* Dev-only staged-run switch: accept a non-vendor one-shot carrier
         * target (for example under /data/local/tmp). Valid only with
         * --run-cve-2026-43284; any other combination is a parse error. */
        bool allow_dev_target = false;
        Cve43284HookGuard run_hook_guard = Cve43284HookGuard::Skip;
        /* B5-9h-3 explicit policy: allow the runner to rewrite a mismatched
         * vermagic in the carrier image before it is written. Off by default. */
        bool allow_vermagic_rewrite = false;
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
        /* A --cve43284-* asset selector without --run-cve-2026-43284. */
        Cve43284OptionRequiresRun,
        /* --cve43284-hook-guard was not reject|skip. */
        BadHookGuard,
        /* delta-4: --plugin without --run-cve-2026-43284 (dev/gate-only flag). */
        PluginRequiresRun,
    };

    /* Parses argv[1..argc). On success fills the output and returns true with
     * error == None. On failure the output is reset and error names the first
     * failing rule; the caller maps it to the existing stderr text. */
    [[nodiscard]] bool parse_arguments(int argc, char *const *argv, Options &out,
                                       ParseError &error) noexcept;
} // namespace ghostlock::support::cli

#endif
