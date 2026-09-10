# Xperia 1 V first-success snapshot

This directory preserves the earliest native GhostLock binary left by the
original Xperia A301SO test series, before the resident 5.15 writer and
policycap-repair experiments.

- Binary SHA-256: `0d567bc6a80a8be3e90f2f105f6fa3872a5c6261dd81faac15489ed01ce9066b`
- Target: A301SO, firmware 67.2.A.3.178
- Kernel: `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- CPU pair used for reproduction: 3/4
- Execution context: `runas_app`, UID 10506, seccomp disabled

The 2026-09-10 reproduction reached the mm_struct leak and payload setup, then
hung after `consumer thread running on cpu=4` and rebooted before the PI route
returned. SELinux remained enforcing. See `reproduction.log`.

`ghostlock-v4-fastroot` is derived from the later V3 binary that first reached
W1, W2, uid 0, and KernelSU on this device. It changes one AArch64 instruction:
the `sleep(2)` call between W2 success and root-child dispatch becomes a NOP.
This lets policy recovery start before Sony's init observes a prolonged
permissive state. The transformation is reproducible with
`tools/patch_xperia_fast_root.py`.

The optional `--bounded-w1` output also limits W1 to two attempts per process.
Repeated misses accumulate stale futex state; the fifth attempt of the
2026-09-10 fast-root run rebooted the device before reaching W2.
