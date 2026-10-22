[English](kernel-phys-offset-plan.md) | [简体中文](kernel-phys-offset-plan_ZH.md)

# Plan for the `kernel_phys_offset` profile field, DRAM base (2026-10-01)

## Status and baseline

- Branch `main`, baseline `2d9d801`, including the complete profile fields from
  `02369c1` and decimal extractor output.
- Triggering device: MTK6893, moto edge 40, `lyriq`,
  `6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`.
- Recorded device logs in `ghostlock-direct-*.log`:
  - `soc: mtk; kernel_phys_load=0x40080000`, with the physical load address
    loaded correctly.
  - `[-] W1: SELinux: target 0x0000000000000000 is outside the direct map, not attempting`
  - `[-] Write 1 failed`, `native exited code=1`.
- Root cause: `ResolvedAddresses::data_alias_checked` in
  `src/core/memory/address_space.cpp` uses the compile-time constant
  `kernel::P0_PHYS_OFFSET = target::address::kPhysicalOffset = 0x80000000`
  from `src/core/kernel/target_constants.hpp:15` as the DRAM base:

  ```cpp
  const auto physical = kernel_phys_load.checked_add(offset);
  if (!physical || physical->value() < kernel::P0_PHYS_OFFSET) return std::nullopt;
  const uintptr_t direct = (physical->value() - kernel::P0_PHYS_OFFSET) | kernel::P0_PAGE_OFFSET;
  ```

  On this device, `kernel_phys_load = 0x40080000 < 0x80000000`, so `physical`
  is always below `P0_PHYS_OFFSET`. All addresses are rejected as out of
  bounds, producing `target=0`.
- This DRAM base was not a profile field. Neither native `profile::KernelMisc`
  nor Kotlin `NativeProfile` had it.
- `kernel_phys_load` and `P0_PHYS_OFFSET` are different quantities. The former
  is the physical address of the kernel image's `_text`, configurable and
  already aligned. The latter is the DRAM base used for image→direct-map conversion.

## Goals and constraints

1. Add the `kernel_phys_offset` profile field for the DRAM base. Keep Kotlin
   `NativeProfile` and native `KernelMisc`/`kSections` aligned.
2. Use this value in `data_alias_checked` and related logs. Fall back to the
   compile-time `P0_PHYS_OFFSET` when the field is absent.
3. Update the extractor, documentation, and UI together. The extractor can
   output the field, the editor displays it, and the schema describes its meaning.

Non-goals:

- Do not change `kernel_phys_load` semantics, the wire version, routes, or attack stages.
- Do not infer the DRAM base automatically at runtime.

Constraints:

- This affects the critical attack path through address resolution and resource
  preparation. It requires `cmp_disasm` for 8 functions, a device gate, and archiving.
- Replace `P0_PHYS_OFFSET` in the `util.cpp` logs with `addresses.phys_offset()`
  as well. Otherwise, logs and behavior disagree.

## Changes implemented

- Native: add `std::optional<uint64_t> kernel_phys_offset` to `KernelMisc`;
  add `OPT("kernel_phys_offset", ...)` to `binary.cpp`; add `phys_offset`
  and an accessor to `ResolvedAddresses`. Parse the field in `init_for_soc`,
  defaulting to `P0_PHYS_OFFSET`. Use the value in `data_alias_checked`
  and the `util.cpp` logs.
- Kotlin: add `kernelPhysOffset: ULong?` and the `kernel_phys_offset` key
  to `NativeProfileDocument`. Update `FieldLabels`, en/zh `strings.xml`,
  and `completeProfileFields` together.
- Extractor: `render_conf` outputs `kernel_phys_offset`, defaulting to `null`.
  Add the field to `ConfInputs`. Assembly in `main` defaults to `None`.
- Documentation: `PROFILE_SCHEMA.md` / `_ZH.md` describe the semantics and default.
- Built-in profiles: add `kernel_phys_offset` to 56 device profiles.
  Of these, 55 use `null` and lyriq uses `1073741824`.

Invariants:

- Behavior for existing devices without `kernel_phys_offset` stays byte-for-byte
  unchanged through the fallback to `P0_PHYS_OFFSET`.
- The image→direct-map sequence stays unchanged. Only the constant becomes configurable.
- The attack state machine, waiter/PI lifetimes, and payload layout stay unchanged.

## Disassembly review record, cmp_disasm

- Baseline: the binary produced by `buildGhostlockNative` after using
  `git stash` to set aside these changes. Candidate: a build with the changes.
- Results for 8 attack functions:
  - `owner_thread` / `consumer_thread` / `run_main_route_threads` /
    `do_kernel5_fake_lock_route` / `do_one_write`: IDENTICAL (strict).
  - `multicast_owner_worker` / `multicast_waiter_worker`: absent from both
    binaries because they were not instantiated.
  - `waiter_thread`: DIFF, base=895 / cur=892, reviewed:
    - Differences are limited to member offset shifts in `g_exploit_session`
      caused by adding `phys_offset` to `ResolvedAddresses`, such as
      `ldr w0,[x21,#0x190]` → `#0x140`; the resulting `adrp/add` data page
      base shifts; and 3 trailing alignment padding instructions.
    - After normalizing immediates, absolute addresses, and symbol names,
      the mnemonic and register sequences match instruction by instruction.
      Only the final 3 alignment lines remain.
    - Conclusion: PI/waiter control flow and step order are unchanged.
      This is a reviewed layout difference that preserves the waiter
      lifetime invariant.
- Lint: `make -C src lint-tidy` rc=0, with 0 findings.

## Decision record

- D1: name the field `kernel_phys_offset`, alongside `kernel_phys_load`.
  Implemented in `profile-core/src/main/kotlin/com/ghostlock/app/data/NativeProfile.kt:29`.
- D2: default to `P0_PHYS_OFFSET (0x80000000)`.
  Implemented in `src/core/memory/address_space.cpp:86-87`.
- D3: the extractor later uses `iomem.rs` to infer the DRAM base and physical
  load address from `/proc/iomem`, commit `81b25c01`. It still outputs
  `null` when no usable iomem is available.
- D4: moto MTK6893 uses `0x40000000` in
  `app/src/main/assets/kernel_profiles/6.6.127-android15-8-gb947b5758b2a-ab15580855-4k.conf:9`.

## Verification evidence

- [x] `make -C src native-host-tests`: added `address_space` cases. A
  profile-provided offset produces the correct direct-map address.
  The default `0x80000000` path stays unchanged.
- [x] `./gradlew :profile-core:test` / `:app:testDebugUnitTest`: fields agree
  on both sides, and the round-trip includes `kernel_phys_offset`.
- [x] `(cd tools/extract_rs && cargo test)`: conf includes `kernel_phys_offset`.
- [x] `cmp_disasm`, as recorded above, with `waiter_thread` reviewed.
- [x] `make -C src lint-tidy` rc=0.

## Remaining work

- [ ] Archive the device gate: cold device, fixed CPU pair, single route.
  Confirm that W1 no longer reports `target=0x0`. Archive under
  `docs/analysis/device-gates/`, removed from the current working tree.
  For the history, see `git show 064cd1e5^:docs/analysis/device-gates/`.
