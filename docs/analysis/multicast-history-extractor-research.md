# Multicast history and extractor research notes

Status: research record, 2026-10-20. Purpose: capture the historical multicast work and the concrete extractor changes it implies. This is not a supported-device claim.

## 1. Why this document exists

Extractor output is sometimes treated as a route-availability oracle. The history shows it is not:

- extractor may omit a route because it cannot derive geometry;
- a missing field is not proof the route is unusable;
- a derived field is not proof the route works at runtime.

The real chain is:

~~~
image geometry -> candidate profile -> device PROBE -> runtime calls/success/status -> device gate
~~~

## 2. Commit timeline

| Commit | Meaning |
|---|---|
| 55e9ed0c | PD2361 5.15.178 manual multicast geometry research |
| a5a72434 | on-device multicast bugs: resident config, one-shot W1 bit16, resident write miss |
| 1b453871 | one-shot-only multicast, offline geometry, remove resident plan |
| 2255b60a | re-poison/re-walk instead of single shot |
| d87e108a | multicast handshake as a decision (`MulticastLimits` / `run_multicast_handshake`) |
| 5236666b | multicast window body and warm-up rounds |
| 6a6a2404 | align with authoritative old implementation; multicast route/consumer/Linux edge |
| d01e9bf6 | all three routes can build access objects on device (not write success) |

## 3. PD2361 manual geometry (5.15.178)

Source: commit 55e9ed0c, `docs/analysis/pd2361-5.15.178-manual-geometry.md`.

Image: PD2361 OTA `payload.bin` -> `boot.img`, release `5.15.178-g3575c47dc7ce-dirty`.

### 3.1 setsockopt chain

~~~
__arm64_sys_setsockopt   0x10
__sys_setsockopt         0x80
sock_common_setsockopt   0x40
udp_setsockopt           0x10
ip_setsockopt            0x290
total                    0x370
~~~

`ip_setsockopt`: `greqs = sp + 0x18`, copy window `0x108`; greqs depth = `0x370 - 0x18 = 0x358`.

### 3.2 futex chain

~~~
__arm64_sys_futex        0xa0
do_futex                 0xd0
futex_wait_requeue_pi    0x1c0
total                    0x330
~~~

`x27 = sp + 0x28`; waiter = `sp + 0x28`; waiter depth = `0x330 - 0x28 = 0x308`.

### 3.3 Candidate fields

| Field | Value | Basis | Confidence |
|---|---:|---|---|
| compact_waiter | 1 | BTF `sizeof(rt_mutex_waiter)=0x58` | high |
| task_offset | 48 | BTF `rt_mutex_waiter.task` | high |
| lock_offset | 56 | BTF `rt_mutex_waiter.lock` | high |
| buffer_size | 264 (0x108) | `ip_setsockopt` copy window | high |
| waiter_off | 80 (0x50) | `0x358 - 0x308` | medium; device PROBE required |
| kernelsnitch.collisions | 8 | GKI 5.15 family default | low |
| kernelsnitch.mm_struct_sz | 1024 | BTF mm_struct + SLUB alignment | medium |

Not statically derivable:

- `fake_lock_offset`, `fake_task_offset`, `lock_slots_*`: depend on `z_pagemap_global` BSS neighbourhood;
- `kernel_phys_load`: bootloader/SoC fact, read from `/proc/iomem` `Kernel code`;
- `waiter_off`: public method requires `MCAST_PROBE` and reading `x27` after panic.

## 4. Known one-shot/resident findings

Source: a5a72434, `docs/analysis/known-bugs-2026-09-26-multicast.md`.

- `multicast_resident` config path was wrong; value always 0.
- one-shot W1 is probabilistic because the carried page must satisfy:

~~~
(right = page_base + 0x100) >> 16 & 1 == 1
~~~

- page-aligned pages have bit16=0 and are refused; one-shot is not reliable for W1.
- resident W1 on A301SO wrote 15/15 with ret=0 but had no effect; cold-boot recheck required.
- extractor previously missed 5.15 `kernelsnitch`; it also over-extrapolated Xperia fake layout to a family.

## 5. Device evidence

`docs/analysis/device-gates/A2-1-20261003-multicast-direct-pass.md`:

~~~
A301SO
5.15.189-android13-8-00016-g51bba4309aac-ab14546557
KernelSU not loaded, Enforcing, uid 2000
multicast route status=0 clean=1/1 step=0 attempts=16 calls=1 success=1
[+] child is root!
su -c id -> uid=0(root)
~~~

This is the strongest historical multicast evidence: not just access construction, but a real root child.

## 6. What d01e9bf6 actually proved

Commit message:

~~~
typed: assembled 43499 route=1 access=1 refusal=
typed: assembled 43499 route=2 access=2 refusal=
typed: assembled 43499 route=3 access=3 refusal=
typed: assembled 43499 close=true
~~~

It proves all three routes can build access objects and close cleanly. It does not prove write/root for select or TCP.

## 7. Extractor logic observed on the AVD

For the API33 AVD kernel (`5.15.119-android13-8...`):

~~~
auto: available cve_2026_43499 = [ mcast_rootchild ]
select: pselect_waiter_shift derivation failed
        no family fallback is defined for this release
        no image-derived geometry; writing an unverified candidate route branch
tcp:    no image-derived geometry; writing an unverified candidate route branch
~~~

Forced select output contains `waiter_shift = null`; forced TCP output contains `tcp_zerocopy {}`.

Conclusion: `available` is an extractor confidence list, not a runtime capability list. On 5.15 the pselect table only covers some families, so select can be skipped even when the route might be usable after manual geometry/PROBE.

API35/6.6 AVD cross-check: extractor derives `pselect_waiter_shift=-2`, but the current select JNI test still reports `statusStep=61, calls=1, successes=0, waiterParkErrno=110`; see `docs/analysis/device-gates/cve43499-select-api35-avd-20261020-1826.md`. Derivation alone is not success.

## 8. Extractor changes to make

### 8.1 Separate confidence from availability

Emit, per route:

~~~
route_candidates = [ multicast_waiter, select_stack, tcp_zerocopy ]
derived = { ... }
missing = { ... }
requires_device_probe = [ waiter_off, fake_lock_offset, kernel_phys_load, ... ]
confidence = high|medium|low
~~~

Do not collapse `missing` into `unavailable`.

### 8.2 Multicast derivation

- derive `waiter_off = setsockopt_depth - futex_depth`;
- derive `buffer_size` from the setsockopt copy window;
- derive compact/task/lock from BTF when present;
- never invent fake_* or phys;
- for 5.15, emit a candidate and mark PROBE-required instead of silently selecting only mcast.

### 8.3 Select derivation

- disassemble `__arm64_sys_futex -> do_futex -> futex_wait_requeue_pi`;
- derive `waiter_word` / `PSELECT_SHIFT` from the stack local;
- if not unique, emit candidate values plus the ambiguity list;
- do not treat `pselect_waiter_shift = None` as route absence.

### 8.4 TCP derivation

- derive carrier/page/zerocopy geometry, not an empty `tcp_zerocopy {}`;
- if geometry is missing, emit `tcp_zerocopy { requires_device_probe = ... }`;
- never write an empty route block that looks usable.

### 8.5 Tests

- 5.15 with missing pselect shift must emit select candidate + explicit missing set;
- forced TCP must not emit an empty route as success;
- BTF absent must not trigger family fallback;
- golden tests for PD2361 values (0x50/264/48/56) and the 5.15 bit16 rule.

## 9. Runtime acceptance (not extractor)

Multicast:

~~~
status=0
calls>0 && success>0
step=0
clean=1/1
child uid=0
~~~

Select (from JoinChang/ghostlock-oneplus logic):

~~~
waiter global_word = PSELECT_SHIFT + waiter_word must be in user-controlled words
calls>0 && success>0
ret == PSELECT_EXPECTED_READY (9)
route_verified via CFI/custom write
~~~

TCP:

~~~
prepare must pass page/fake checks
zerocopy/punch path must return a named result
no empty FakePage; no step=40 default
~~~

## 10. Sources

- commits: 55e9ed0c, a5a72434, 1b453871, 2255b60a, d87e108a, 5236666b, 6a6a2404, d01e9bf6
- `docs/analysis/pd2361-5.15.178-manual-geometry.md` (content in commit 55e9ed0c)
- `docs/analysis/known-bugs-2026-09-26-multicast.md`
- `docs/analysis/remove-multicast-resident-plan.md`
- `docs/analysis/device-gates/A2-1-20261003-multicast-direct-pass.md`
- `https://github.com/JoinChang/ghostlock-oneplus` README + `src/core/fops.c`
- `docs/analysis/avd-43499-kernel-ranchu-20261020.conf`
