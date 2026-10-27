# W2 CPU-affinity recovery plan (2026-10-25)

## Current state and evidence

Latest device run:
`/storage/emulated/0/Download/ghostlock-debug-log/20261025-173702/ghostlock-direct-0.log.txt`.
W1 completes, W2 starts, and the run emits repeated
`sched_setaffinity(...): Invalid argument` messages before native exit `255`.
W3 is not reached. A previous run at `20261025-173314` reaches W3 and reports
`child seccomp filter bypassed (finit_module errno=22)`, so this batch does not change
the seccomp bypass.

The likely failing operation is the KernelSnitch affinity reset: `reset_cpu_pin()`
passes an all-bits mask to `sched_setaffinity()` instead of the process's valid
allowed mask. The selected pair `4,5` is also not validated against the live affinity
mask before worker creation.

## Goal and constraints

- Keep W1/W2/W3 ordering and attack-window machine-code shape unchanged except for
  affinity setup/recovery.
- Restore only CPUs allowed to the current process; never substitute an arbitrary CPU.
- Preserve `kernelsnitch/` source unless the minimal affinity helper change is required.
- Do not alter profile offsets, route parameters, payload layout, or seccomp writes.

## Planned change

1. Capture the caller's allowed CPU mask before KernelSnitch pins the scan thread.
2. Restore that captured mask instead of constructing an all-bits mask.
3. Validate selected main/consumer CPUs against the live affinity mask before native
   execution and fail with a logged profile/configuration error when either CPU is not
   allowed.
4. Add host tests for mask validation and restore behavior.

## Verification

- `make -C src native-host-tests`
- NDK build with zero warnings
- `make -C src lint-tidy`
- `tools/cmp_disasm.py` against the confirmed baseline for all eight attack functions
- cold-device rerun on the same kernel/profile, recording W2 and W3 results

## Explicit preservation

No changes to W1/W2/W3 target addresses, write modes, routes, victim lifetime,
seccomp offsets, or KernelSU handoff are planned.

## Status

- [x] Read latest device log and compare with previous successful run.
- [x] Identify W2 affinity failure before W3.
- [x] Implement saved-affinity restoration in KernelSnitch.
- [x] Run available host checks; full native/NDK and device verification remain pending.
