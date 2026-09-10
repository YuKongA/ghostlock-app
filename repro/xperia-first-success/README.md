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

V12 removed the global zero-page exposure and reached uid 0 twice, but PID 1
aborted while the compact erase temporarily corrupted static `init_cred + 8`.
BTF confirms Xperia's `struct cred` is 176 bytes with `security` at `+0x78`.
V13 embeds the exact 176-byte `init_cred` template from 67.2.A.3.178 (raising
only `usage`) in the pinned payload and makes W2 point to that private copy.
This removes the static `init_cred` side write and the W2b repair stage.

V13 reached the W2 route with the complete private template, then rebooted
before the uid probe because compact erase also overwrites the chosen private
cred's `+8` word. V14 quarantines that socket, repairs private cred `+8` while
the victim remains parked, and only then asks the child to execute `getuid`.
A successful rooted child keeps the repaired backing page pinned.

V14 repaired the private cred before probing but still rebooted because the
four static pointers copied from the on-disk Image were pre-KASLR canonical
addresses. V15 converts those image VAs through `data_addr()` so the private
credential refers to valid direct-map aliases at runtime.

The V16 slide-only probe restored the historical consumer timing and compact
field placement, but Xperia's working primitive additionally needs the newer
multicast ghost route, so it never changed `boot_id`. V17 returns W2 to the
real `init_cred` and prebuilds a leaf repair payload before W2. After W2 it
swaps directly to that pinned payload and fires `init_cred + 8` repair without
running another collision search, reducing PID 1's exposure to the route time.

V17 proved the prebuilt repair path: W2b completed and the victim reported
uid 0, but PID 1 still aborted after the roughly two-second waiter route.
V18 gives only the prebuilt repair route a 250 ms absolute futex timeout. The
normal W1/W2 timing remains unchanged; repair should land about 750 ms sooner,
while the owner-thread join may finish later without extending corruption.

V18 survived one isolated run but later real-loader runs rebooted before the
root script, with an empty loader log and PID 1 abort evidence. KernelSU was
not involved. V19 reduces only the prebuilt repair wait from 250 ms to 100 ms;
the trigger still starts after 50 ms, leaving a 50 ms scheduling window.

V19 still rebooted after a successful isolated uid-0 run, so 100 ms was not
short enough. V20 uses a 5 ms trigger delay and 20 ms waiter timeout only for
the prebuilt W2b route, leaving roughly 15 ms for the consumer race. Normal
W1 and W2 retain their established 50 ms / 1 s timing.

V20 passed the isolated-loader test and subsequently completed a real
KernelSU late-load on A301SO firmware 67.2.A.3.178. Both `su -c id` and
`su 0 id` returned uid/gid 0 in `u:r:ksu:s0`; `/proc/modules` reported
`kernelsu` Live while SELinux remained Enforcing and Android's activity and
package services remained available. One preceding real-loader attempt
rebooted during W1, before W2 or ksud execution, so W1 remains stochastic.
The successful console transcript is saved as `v20-real-ksu-success.log`.

V21 rebuilds V20 from the recovered multicast writer source and verifies that
`load_policy` advances the SELinux status sequence before KernelSU late-load.
This prevents a false zero exit status from being treated as successful policy
recovery. Its binary is `ghostlock-v21-policyload-guard` with SHA-256
`e2b9aad19223c6b2731b46733c7fb0ed0cc27114369ac6d52165dd886512c253`.
