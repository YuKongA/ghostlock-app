#ifndef GHOSTLOCK_SUPPORT_CLI_HPP
#define GHOSTLOCK_SUPPORT_CLI_HPP

/* Process command-line parsing for the native ghostlock executable.
 *
 * The parser is split out of core/main.cpp so the flag surface (and its
 * mutual-exclusion rules) is host-testable without linking the attack runtime.
 * It performs no I/O and touches no global state.
 *
 * S4 R2b: the CLI carries TRANSPORT / RUN CONTROL / SAFETY / OBSERVABILITY
 * only. Every selection and every policy value comes from the GLKv3 document
 * (the backend.<id>.steps token plus the owner sections); the staged
 * --run-cve-2026-43284 entry and its asset selectors are gone, so a dev replay
 * runs the same document through the same Pipeline as production.
 *
 * Entry-point modes are exclusive: exactly zero or one of
 * --ghostlock-app-call, --load-prebuilt-profile and --probe-cve-2026-43284 may
 * be given. --force-attack / --dump-kernel-log / --enable-status-record modify
 * the app/prebuilt modes; the read-only probe rejects all of them plus the
 * safety switch so it can never inherit a side effect. --allow-dev-target is a
 * SAFETY switch of the cve_2026_43284 production path (a non-vendor one-shot
 * carrier for device gates): it relaxes exactly that check and carries no data.
 *
 * DELETED in R2b (the former selection/policy surface that bypassed Pipeline):
 * --run-cve-2026-43284, --stage, --plugin and the --cve43284-* asset selectors
 * (hook-target / hook-symbol / hook-guard / carrier / patch1-target /
 * allow-vermagic-rewrite). The carrier and module path are document fields;
 * the remaining hook assets are production constants. Unknown flags are a
 * parse error, so a stale dev command fails closed instead of silently
 * degrading to a profile-less run. */

#include <cstdint>

namespace ghostlock::support::cli {
    enum class Mode : std::uint8_t {
        None = 0,
        AppCall,
        PrebuiltFile,
        ProbeCve2026_43284,
        PluginProbe,
    };

    struct Options final {
        /* Pointers first: this keeps Options at one byte of tail padding, which
         * clang-tidy's performance.Padding check enforces. */
        const char *load_prebuilt_profile = nullptr;
        const char *dump_kernel_log = nullptr;
        /* Non-null only for Mode::ProbeCve2026_43284. */
        const char *probe_module_path = nullptr;
        /* Non-null only for Mode::PluginProbe (the .so to describe). */
        const char *plugin_probe_path = nullptr;
        /* --expect-sha256 <hex>: only valid with --plugin-probe. */
        const char *expect_sha256 = nullptr;
        Mode mode = Mode::None;
        /* Safety switch (closed list; data never authorizes): accept a
         * non-vendor one-shot carrier given as an absolute path. Valid with the
         * app-call/prebuilt modes; the read-only probe rejects it. */
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
        /* The read-only probe combined with a switch that would give it a side
         * effect (--force-attack / --dump-kernel-log / --enable-status-record /
         * --allow-dev-target). */
        ProbeConflict,
        /* --expect-sha256 without --plugin-probe. */
        ExpectShaRequiresPluginProbe,
        /* A --plugin-probe input combined with a switch that would give the
         * read-only probe a side effect. */
        PluginProbeConflict,
    };

    /* Parses argv[1..argc). On success fills the output and returns true with
     * error == None. On failure the output is reset and error names the first
     * failing rule; the caller maps it to the existing stderr text. */
    [[nodiscard]] bool parse_arguments(int argc, char *const *argv, Options &out,
                                       ParseError &error) noexcept;
} // namespace ghostlock::support::cli

#endif
