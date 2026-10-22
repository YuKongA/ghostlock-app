[English](profile-editor-complete-fields-plan.md) | [简体中文](profile-editor-complete-fields-plan_ZH.md)

# Plan for complete profile editor fields and HOCON completeness (2026-09-30)

## Status and baseline

- Branch `very-not-stable-dev`; baseline commit `deff0b1`, Merge branch
  'ancillary-architecture'.
- Trigger: while adapting the MTK moto edge 40,
  `6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`, `kernel_phys_load`
  was missing from the UI, and Kotlin could not read the extractor's conf.
- Baseline behavior:
  1. The "Parameter overrides" page, `ProfileOverrideScreen`, only renders
     `ExecutionEditor`. Its fields come from `ProfileConfig.GeneralPaths`
     in `GhostlockModels.kt:95`: 9 `execution.*` entries plus tuning for
     the current route. `kernel_phys_load`, `task_struct.*`, `cred.*`,
     and `offset.*` do not appear.
  2. The "Advanced parameter overrides" page, `AdvancedOverrideScreen`,
     uses `buildTree(full)` to traverse only keys present in the resolved
     profile. The `value is Number` / `value == null` checks skip string
     values. Keys absent from the profile cannot be edited.
  3. The extractor's `render_conf` omits entries without values and
     outputs `kernel_phys_load` as unquoted hexadecimal, `0x{phys:X}`.
  4. HOCON, Typesafe Config, treats `0x...` as an unquoted string.
     Kotlin's `getLongAt` in `ValueModel.kt:56` accepts only `Number`,
     so it silently drops the extractor's `kernel_phys_load`. The recorded
     test returned `String` for `0x40080000` and `Integer` for `1074266112`.
  5. Built-in profiles use `include` and do not contain all fields.

## Goals and constraints

1. The UI editor loads every editable entry for the current route. Entries
   missing from the profile appear as editable `null` placeholders.
2. All built-in or generated HOCON includes every entry: common geometry,
   the current route, and fallback. Infer values automatically where
   possible. Otherwise, write `null`.
3. The extractor outputs only decimal numbers, with no `0x`, strings, or
   scientific notation, and writes every entry.
4. Writes and exports preserve every entry, including `null`.

Non-goals:

- Do not change the attack path, the waiter/race/payload/route/exec flow,
  or native parsing logic in `src/core/`.
- Do not add routes or components, or change the GLK1 wire and HOCON
  transport format version. It remains v2.
- Do not change `kernelsnitch/`, the v1 conversion in
  `LegacyProfileConverter.kt`, or the explicit preservation list
  in the standards.
- Do not change correct values in existing built-in profiles.
  Only complete the fields and their representation.

## Changes implemented

| File | Change |
|---|---|
| `tools/extract_rs/src/report.rs` | Output `kernel_phys_load` in decimal. Write `null` for missing entries in `conf_offsets`/task/cred/route geometry/snitch. Include placeholders for all geometry fields of the current route |
| `tools/extract_rs/src/main.rs` | Pass the complete field set, including null placeholders, when assembling conf |
| `app/src/main/assets/kernel_profiles/*.conf` | Complete the common and route fields in each profile, writing `null` for unknown values |
| `app/src/main/kotlin/.../data/AndroidProfileConfigController.kt` | `buildTree`/`generalFields`/`routeTuningPaths` render the full set of current route, fallback, and common fields, filling missing entries with `null`. `buildTree` skips `execution` |
| `app/src/main/kotlin/.../domain/model/GhostlockModels.kt` | Expand `GeneralPaths` to include all execution fields |
| `profile-core/.../data/HoconSupport.kt` | Check how `render` outputs `null` and add tests |
| `docs/kernel_profiles/PROFILE_SCHEMA*.md` | Document the field completeness rules |

The dataflow changes from allowing fields to disappear silently at each layer
to keeping presence explicit and fields complete:

```
extractor conf --(decimal + null placeholders)--> HOCON(every entry) --(preserve Number/null)--> resolved map
   --(authoritative full set + null placeholders)--> complete UI listing --(editable/write back)--> export/run
```

Invariants:

- The critical attack path and native parsing stay unchanged.
- v2 semantics still use whether a key is present. D3 defines the distinction
  between `null` and an explicitly supplied 0.

## Decision record

- D1: full loading includes common entries. The UI editor lists the current
  route geometry, fallback, and common geometry,
  `task_struct`/`cred`/`offset`/`kernel`/`kernelsnitch`.
  Missing entries display `null`.
- D2: execution belongs on the general editor page. `execution.*` is fully
  loaded in `ProfileOverrideScreen`. It does not appear in the advanced
  tree and is not inlined into device profile HOCON.
- D3=A: `null` means the key is absent from the GLK1 binary. Native reads
  it as not provided. HOCON/UI keeps `null` only as a placeholder.
  This does not change wire presence semantics or introduce a presence bitmap.
- D4: manually add `null` to each built-in profile. There are 54 device
  profiles at baseline `deff0b1`.
- D5: executable branches. The extractor outputs only the selected executable
  route branch. When a fallback exists, it also outputs the fallback
  branch's geometry and writes the actual route to `fallback.to`.

GLK1 v2 is the transport format between Kotlin and native. It also carries
built-in precompiled profiles: `exportKernelProfiles` produces `.bin` files,
and native loads them with `--load-prebuilt-profile`. It is neither the
storage format for user edits, which use HOCON/preferences, nor an execution
format. D3=A therefore changes only HOCON/UI/extractor, leaving the GLK1
byte format unchanged.

## 5.15 multicast→select fallback, deferred

Status: deferred by user decision. This batch does not touch
`route_policy.hpp`, and the extractor does not output a fallback branch.

Requirement: when a 5.15 device has both mcast geometry and derivable
pselect settings, use multicast first and fall back to select on failure.

- Baseline: the fallback branch in `run_route_policy` is constrained by
  `Policy::allows_fallback` in `route_policy.hpp:237`. `SelectPolicy`
  and `MulticastPolicy` inherit the default `false`. Only `TcpPolicy`
  uses `true`, in `route_policy.hpp:93`. The extractor hardcodes
  fallback to `to="none"`.
- Planned change: `MulticastPolicy::allows_fallback = true`. When pselect
  is available, the extractor outputs `fallback.to="select_stack"` and
  `fallback.route.select_stack.waiter_shift`.
- Blocker: fallback switches only `run()`, but the backend's W2/W3
  capabilities are still compiled for the primary policy,
  `multicast=true` and `w2_fast_repair=true`, with hooks defined in
  `MulticastPolicy`. There are two ways to handle whether these
  capabilities and hooks remain valid after falling back to select:
  - (a) Allow multicast to fall back only at specific clean failure
    points, and do not enter the multicast-specific W2 fast repair
    after fallback.
  - (b) Add a separate capability check for fallback and prohibit
    W2/W3 reuse across primitives.
  Add a control flow diagram and invariants before implementation.
- Verification gates, since this touches the attack path: `cmp_disasm`
  for 8 functions, a cold-device gate, and archiving.
- Still awaiting confirmation: whether to do only extractor/UI/HOCON
  first, without touching native, and handle native fallback in a
  separate batch.

## Clarified non-blocker

The branchField mapping in `ProfileResolver.nativeValue` maps
`route.tcp_zerocopy.compact_waiter` to native `misc.compact_waiter`,
through `OPT("compact_waiter", misc.compact_waiter)` in `binary.cpp:113`.
This agrees with the 6.1 built-in profiles and causes no conflict.
The complete route HOCON field set is `tcp_zerocopy.{compact_waiter}`,
`select_stack.{waiter_shift}`, and
`multicast_waiter.{waiter_off,buffer_size,task_offset,lock_offset,compact_waiter}`.

## Compatibility and rollback

Old built-in profiles still parse because all added fields are optional
or null. Files are independent and can be reverted individually.
The extractor's output format change affects only newly generated conf files.

## Verification evidence

- [x] `cargo test --release --manifest-path tools/extract_rs/Cargo.toml`:
  all passed, including new cases for decimal conf output and complete
  fields with null.
- [x] `./gradlew :app:testDebugUnitTest`, `:profile-core:test`: all passed.
- [x] `./gradlew exportKernelProfiles`: exported `.bin` files include
  `kernel_phys_load` and other fields.
- [x] Built-in profiles completed: top-level `kernel_phys_load`,
  `offset.empty_zero_page`, and `cred.usage_offset`. Shared
  `credential-6x.conf`/`kernelsnitch-6x.conf` completed.

## Remaining work

- [ ] Native route-aware fallback, deferred. See the blocker above.
- [ ] Device gate: load the new profile and confirm route selection and
  write verification pass. The change does not touch the native attack
  path, but field completeness still needs one device check.
