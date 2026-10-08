/* Host test for the S4 R2b command-line surface.
 *
 * The CLI carries transport / run control / safety / observability only:
 * selection and policy come from the GLKv3 document. This test pins the kept
 * flag set, the entry-point exclusivity, the read-only probe conflicts and the
 * NEGATIVE contract: every flag R2b deleted (the staged --run-cve-2026-43284
 * entry, --stage, --plugin and the --cve43284-* asset selectors) is now an
 * unknown argument, so a stale dev command fails closed instead of silently
 * degrading to a profile-less run. */

#include "support/cli.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using ghostlock::support::cli::Mode;
    using ghostlock::support::cli::Options;
    using ghostlock::support::cli::ParseError;

    /* The parsed Options stores raw char* into the argument storage, so the
     * backing strings must outlive the call; a function-local static keeps them
     * valid until the next parse (each case asserts before parsing again). */
    ParseError parse_args(std::vector<std::string> args, Options &opts) {
        static std::vector<std::string> storage;
        storage = std::move(args);
        static char program[] = "ghostlock";
        std::vector<char *> argv;
        argv.reserve(storage.size() + 1U);
        argv.push_back(program);
        for (std::string &arg : storage) {
            argv.push_back(arg.data());
        }
        ParseError error = ParseError::None;
        (void)ghostlock::support::cli::parse_arguments(
                static_cast<int>(argv.size()), argv.data(), opts, error);
        return error;
    }

    /* Every flag S4 R2b removed. A stale dev/gate command line must fail
     * closed as an unknown argument. */
    const char *kDeletedFlags[] = {
        "--run-cve-2026-43284",
        "--stage",
        "--stage=plan",
        "--stage=write",
        "--stage=trigger",
        "--stage=full",
        "--plugin",
        "--cve43284-hook-target",
        "--cve43284-hook-symbol",
        "--cve43284-hook-guard",
        "--cve43284-carrier",
        "--cve43284-patch1-target",
        "--cve43284-allow-vermagic-rewrite",
        "--allow-vermagic-rewrite",
    };
} // namespace

int main() {
    /* ---- Transport / run control / safety / observability. ---- */
    {
        Options opts{};
        assert(parse_args({}, opts) == ParseError::None);
        assert(opts.mode == Mode::None);
        assert(!opts.force_attack && !opts.status_record && !opts.allow_dev_target);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call"}, opts) == ParseError::None);
        assert(opts.mode == Mode::AppCall);
        assert(opts.load_prebuilt_profile == nullptr && opts.dump_kernel_log == nullptr);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--enable-status-record",
                           "--dump-kernel-log", "/tmp/glk-logs", "--force-attack",
                           "--allow-dev-target"},
                          opts) == ParseError::None);
        assert(opts.mode == Mode::AppCall);
        assert(opts.status_record && opts.force_attack && opts.allow_dev_target);
        assert(std::string_view(opts.dump_kernel_log) == "/tmp/glk-logs");
    }
    {
        Options opts{};
        assert(parse_args({"--force-attack", "--load-prebuilt-profile", "/tmp/p.bin",
                           "--allow-dev-target"},
                          opts) == ParseError::None);
        assert(opts.mode == Mode::PrebuiltFile);
        assert(std::string_view(opts.load_prebuilt_profile) == "/tmp/p.bin");
        assert(opts.force_attack && opts.allow_dev_target);
        assert(!opts.status_record);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "/tmp/a.ko"}, opts) ==
               ParseError::None);
        assert(opts.mode == Mode::ProbeCve2026_43284);
        assert(std::string_view(opts.probe_module_path) == "/tmp/a.ko");
        assert(!opts.force_attack && !opts.status_record && !opts.allow_dev_target);
    }

    {
        /* S4 P1 plugin probe: read-only description entry. */
        Options opts{};
        assert(parse_args({"--plugin-probe", "/tmp/cm.so"}, opts) == ParseError::None);
        assert(opts.mode == Mode::PluginProbe);
        assert(std::string_view(opts.plugin_probe_path) == "/tmp/cm.so");
        assert(opts.expect_sha256 == nullptr);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe", "/tmp/cm.so", "--expect-sha256",
                           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
                          opts) == ParseError::None);
        assert(opts.mode == Mode::PluginProbe);
        assert(opts.expect_sha256 != nullptr);
    }
    {
        Options opts{};
        assert(parse_args({"--expect-sha256", "aa"}, opts) ==
               ParseError::ExpectShaRequiresPluginProbe);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe", "x", "--force-attack"}, opts) ==
               ParseError::PluginProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe", "x", "--allow-dev-target"}, opts) ==
               ParseError::PluginProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--expect-sha256"}, opts) == ParseError::MissingArgument);
    }

    /* ---- Entry points are exclusive. ---- */
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--load-prebuilt-profile", "x"},
                          opts) == ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--ghostlock-app-call"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--load-prebuilt-profile",
                           "x"},
                          opts) == ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe", "x", "--ghostlock-app-call"}, opts) ==
               ParseError::MultipleEntrypoints);
    }
    {
        Options opts{};
        assert(parse_args({"--plugin-probe", "x", "--probe-cve-2026-43284", "y"}, opts) ==
               ParseError::MultipleEntrypoints);
    }

    /* ---- The read-only probe rejects every side effect. ---- */
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--force-attack"}, opts) ==
               ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--enable-status-record"}, opts) ==
               ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--dump-kernel-log", "d"}, opts) ==
               ParseError::ProbeConflict);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284", "a", "--allow-dev-target"}, opts) ==
               ParseError::ProbeConflict);
    }

    /* ---- Remaining rules. ---- */
    {
        Options opts{};
        assert(parse_args({"--enable-status-record"}, opts) ==
               ParseError::StatusRequiresAppCall);
    }
    {
        Options opts{};
        assert(parse_args({"--probe-cve-2026-43284"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--load-prebuilt-profile"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--dump-kernel-log"}, opts) == ParseError::MissingArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--nope"}, opts) == ParseError::UnknownArgument);
    }

    /* ---- Deleted flags are unknown arguments (fail closed). ---- */
    for (const char *flag : kDeletedFlags) {
        Options opts{};
        assert(parse_args({flag}, opts) == ParseError::UnknownArgument);
    }
    {
        /* The deleted staged entry must not silently consume its positionals. */
        Options opts{};
        assert(parse_args({"--run-cve-2026-43284", "/tmp/a.ko", "/vendor/x.so"}, opts) ==
               ParseError::UnknownArgument);
    }
    {
        Options opts{};
        assert(parse_args({"--ghostlock-app-call", "--cve43284-carrier", "/vendor/x.so"},
                          opts) == ParseError::UnknownArgument);
    }
    puts("support_cli_test: ok");
    return 0;
}
