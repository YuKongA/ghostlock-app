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

`ghostlock-v6-safe-zero` is built from `main` commit `13f425f` with only the
post-W2 `sleep(2)` removed. Its one-shot W1 uses `empty_zero_page` as the
promoted rb-tree child. This avoids placing `selinux_state - 8` in a reclaimed
heap page, which caused ART processes to crash on invalid references such as
`0x2adaad88`. The resident writer remains compiled in but is disabled unless
`GHOSTLOCK_515_RESIDENT` is explicitly set.

V6 proved that `empty_zero_page` is also unsafe as the promoted child: the
erase operation poisoned the global zero page with `selinux_state - 8`, and
ART processes then observed `0x2adaad88` as an object reference. V7 instead
uses the reserved Xperia BSS scratch area at `off_mcast_fake_bss + 0x1240` and
resets `/sys/fs/selinux/checkreqprot` before policy reload.

V7 rebooted immediately after W2, showing that the selected BSS area contains
live kernel state. V8 removes the promoted child entirely for W1 and uses the
existing leaf-zero erase path. This requests a direct zero write without a
secondary destination write-back.

V8 reached uid 0 but rebooted before its root script ran. W2 had the same
secondary-write problem: it promoted the global `init_cred` object even though
the payload already contained a private credential copy. V9 points W2 at that
pinned payload copy, leaving the kernel's static `init_cred` untouched.

V9 rebooted before its first uid report because the minimal credential copy
does not include Xperia's live SELinux security pointer. V10 returns to the
real `init_cred`, then immediately applies a leaf-zero write to `init_cred + 8`,
the secondary write location documented by the compact waiter primitive.

V10 completed the `init_cred + 8` repair but rebooted when policy recovery
started because leaf-zero W1 had also cleared `selinux_state.initialized`.
V11 uses promoted-child W1 to preserve a non-zero initialized byte, immediately
repairs `empty_zero_page + 8`, then performs W2 and its `init_cred + 8` repair.

V11 proved all four write stages but still exposed `empty_zero_page` to the
W1 side write for roughly nine seconds before W1b. New ART crashes carried the
same `0x2adaad88` poison during that window. V12 instead directs W1's secondary
write into `page_base + 0x108`, quarantines the reclaim socket, repairs that
private slot, and only then releases the page. The global zero page is never
used as a promoted child.
