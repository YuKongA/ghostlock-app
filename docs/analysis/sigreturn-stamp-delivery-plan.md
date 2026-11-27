# Plan: sigreturn stamp delivery for the multicast_waiter route

## Background

The multicast_waiter route delivers the encoded waiter stamp into the victim
thread's kernel stack with `setsockopt(IPPROTO_IP, MCAST_BLOCK_SOURCE)`: the
kernel copies the userspace buffer through the IPv4 msfilter path onto the
stack frame of the syscall. This works where the copy window reaches the stale
`rt_mutex_waiter` — on 5.15/6.1 the waiter sits deep enough.

On the shallow-frame android12-5.10 generation (5.10.43/81/101) the geometry
closes. Measured on Black Shark KTUS-A0, `5.10.101-android12-9-gece0cc04d1a5`:

- the IPv4 msfilter copy window carries at most 0x108 bytes at stack depth
  864, while reaching the waiter's `task`/`lock` words needs 480 bytes from
  the window start;
- select_stack cannot substitute: the stale waiter lands at fd word 10 of the
  pselect6 fd bitmaps, and nfds is bounded by `SELECT_STACK_ALLOC` (256 bytes)
  — words 15+ fall in the kernel-zeroed `res_*` bitmaps, so `task`/`lock`/
  `prio` can never be delivered through fd bits.

Result: on this generation neither stock route can deliver a controlled
waiter, while the underlying race bug is fully present and deterministic.

## Approach

Deliver the same encoded stamp through `rt_sigreturn` instead. The kernel's
`restore_fpsimd_context` copies the 512-byte `vregs` array of the signal
frame into a kernel stack buffer at a fixed depth below the syscall-entry
stack pointer. On 5.10.101 (disassembly-verified frame chain
`rt_sigreturn(0x50) → restore_sigframe(0x60) → restore_fpsimd_context(0x250)`)
the buffer covers the stale waiter at `waiter_off = 320` with `task`/`lock`
at stamp offsets 368/376. Unlike the msfilter copy, this copy length is fixed
by the fpsimd record layout, not bounded by socket option plumbing.

The delivery is a one-shot side effect: the thread invokes `rt_sigreturn`
with `sp` pointed at a crafted frame whose GPR slots hold the live register
values and whose `pc` is a continuation label; surviving the restore means
the copy landed, and a frame fault kills only this thread.

Supporting changes:

1. **Flat-waiter tree encoding** (`encode_multicast_waiter_trees`): plant
   `rb_parent_color = target - 8`, `rb_right = rb_left = 0` in both
   `tree_entry` and `pi_tree_entry` of the stamped waiter. `rb_erase` of the
   leaf then deterministically writes NULL over `*(target)`.
2. **Flat prio stamp**: the 5.10 `rt_mutex_waiter` has no `wake_state` word;
   `prio` sits at +0x40. Stamp `FAKE_WAITER_PRIO` there so the erase gate
   (`prio > 120`) passes. Same word upstream PR #721 stamps for select.
3. **Consumer walk cadence**: on 5.15/6.1 `sched_setattr` on the waiter fails
   intermittently and the failure path walks the PI futex queues, which is
   what repositions the stale waiter. On this 5.10 generation the call never
   fails, so the walk never runs. Walk the queues on every consumer call;
   the walk is the same code path 5.15/6.1 already execute frequently, only
   unconditional.
4. **Delivery selection**: one new optional key `stamp_delivery`
   (`mcast` default / `sigreturn`) in the existing `route.multicast_waiter`
   wire section. v2 object sections are presence-based: absent key keeps the
   current behavior byte for byte.
5. **Budgeted window + enforce-flag self-close** (sigreturn mode only): the
   erase lands asynchronously inside the consumer's PI walk. The route keeps
   the window armed until the consumer spends its `consumer_max_calls` budget
   or closes it early — for the `selinux_enforcing` write the consumer polls
   the flag after each call and closes the window the moment it flips. A
   further consume after the flip would walk the chain again and re-erase the
   already dequeued node (observed to panic in development), and tearing the
   window down after only the first call would race the in-flight reposition.
   Other kernels never enter this path: with `stamp_delivery` absent or
   `mcast` the window still closes after the first consumer call.

6. **Fake waiter page priority words**: the shallow-frame 5.10
   `rt_mutex_waiter` has no `wake_state` word; `prio` sits at +0x40. The
   spray page builder must write `FAKE_WAITER_PRIO` at both +0x40 (5.10 flat)
   and +0x44 (6.1 wake_state+prio layout). Writing 0 at +0x40 makes the erase
   gate's priority comparison read prio=0 on this generation.
7. **Exporter CPU pair**: `ProfileExporter` previously hardcoded
   `CpuPairView(0, 1)`, clobbering any profile-authored `selected_cpus` in
   exported GLK1 documents. It now honors an authored pair, so devices whose
   big/prime cores thermally isolate under load (measured pin EINVAL rates on
   cpu5/6/7) can pin main/consumer inside the little cluster.

Requires: the pin-exact spray fixes (interleaved reclaim + inherited
`/proc/<pid>/mem` pin dropping) — without them the target slab page stays
pinned inside the spray window and the write never lands.

## Files

- `src/core/route/sigreturn_stamp.{cpp,h}` — new: frame builder + fire stub
- `src/core/route/multicast_waiter_route.cpp` — delivery variant + prio stamp
- `src/core/memory/payload_builder.{cpp,h}` — tree encoding + stamp reader
- `src/core/race/threads.cpp` — consumer walk cadence
- `src/core/profile/binary.cpp`, `model.h` + Kotlin route config — the
  `stamp_delivery` key (both sides, key name byte-identical)
- `src/core/support/util.cpp` — fake waiter page: prio at both +0x40/+0x44
- `profile-core/.../profile/ProfileExporter.kt` — honor authored selected_cpus
- host tests: stamp builder vectors, tree encoding, key round-trip

## Behavioral differences

- `stamp_delivery` absent or `mcast`: no behavioral change anywhere. The
  sigreturn frame builder is never called, the tree/prio planting never runs,
  and the window closes after the first consumer call exactly as before.
- Consumer walk cadence: on kernels where `sched_setattr` fails, the walk
  already runs on those failures; making it unconditional adds walks on the
  success path too. cadence stays bounded by `consumer_max_calls`. This is
  the one behavioral change visible to 5.15/6.1 and is called out for review.

## Verification

- `make -C src lsprivilege` zero warnings; `native-host-tests` 20/20;
  `lint-tidy` 0 findings
- `tools/cmp_disasm.py` against the pre-change baseline: attack-critical
  instruction streams identical outside the new/changed functions
- Device gate: Black Shark KTUS-A0 (Snapdragon 8 Gen 1 / SM8450),
  `5.10.101-android12-9-gece0cc04d1a5`, cold boot, fixed CPU pair,
  multicast_waiter route with `stamp_delivery=sigreturn` — full chain,
  KernelSU loaded, SELinux restored to enforcing, exit 0, no panic.
  **PASSED on this port — three independent cold-boot replications**
  (rounds 82/108/122, each full chain to uid 0 + KernelSU ready + enforce
  restored; 3/14 pass rate across cold rounds). Same-build panic/PASS
  interleaving matches the documented KERNEL-PANIC-01 jitter — panics land
  only in inter-window transitions, never inside an armed window
