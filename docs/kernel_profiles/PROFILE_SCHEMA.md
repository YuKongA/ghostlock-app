# Kernel Profile Structure (Configuration System)

> 中文: [PROFILE_SCHEMA_ZH.md](PROFILE_SCHEMA_ZH.md)

This document describes the complete structure of the kernel profiles under
`app/src/main/assets/kernel_profiles/`: what each field means, how routes work,
how profiles are validated, and how configuration flows between Kotlin and
native.

> To add support for a new kernel, follow [README.md](README.md). For
> execution-tuning defaults, see [defaults.md](defaults.md).

> **Version naming (v1 / v2 / v3)**: the configuration system has three layers.
> - **v1 = the old JSON format**: the `offsets.json` of the remote/main era and
>   the import / extractor output path. Code: `src/core/legacy/`
>   (`convert_legacy_offsets`) and Kotlin `LegacyProfileConverter`.
> - **v2 = the HOCON profile generation**: `assets/kernel_profiles/*.conf`,
>   parsed and merged by `AndroidProfileConfigController`.
> - **v3 = the current wire**: `NativeProfileDocument` serializes the resolved
>   profile into the binary native consumes, with component ids
>   (frontend / backend / middleware), the middleware section and the options
>   section. Native still decodes v2 documents for compatibility; the v2 writer
>   is gone.
>
> The native binary transport uses magic `0x0D000721` and version `3`
> (version `2` is still decoded for compatibility).

## 0. File format (HOCON)

Built-in profiles, `index.conf`, shared files, and imported offset files are all
parsed as **HOCON**:

- JSON is a subset of HOCON, so historical files need no conversion;
- `#` / `//` comments, trailing commas, and `${var}` substitution (optional
  `${?var}`) are supported;
- `include "file.conf"` is supported (relative to the same directory, nestable,
  loop-safe): `AssetConfigLoader` expands includes when reading assets, while
  imports prefer a file selected alongside the profile and otherwise look in the
  bundled shared files, failing loudly when missing so you can reselect;
- parsing happens on the Kotlin side (`HoconSupport`), then the typed binary
  struct crosses to native (section 9);
- the app stores and exports HOCON; the extractor's `offsets.json` can still be
  imported through the v1 path.

## 1. Data flow

```
assets/kernel_profiles/<release>.conf     built-in profile (HOCON)
assets/kernel_profiles/execution-tuning.conf   general execution tuning preset (all kernels)
assets/kernel_profiles/execution-<route>.conf   per-route execution tuning preset (loaded by the resolver)
assets/kernel_profiles/credential-6x.conf      6.x credential template
assets/kernel_profiles/kernelsnitch-6x.conf    6.x KernelSnitch values
assets/kernel_profiles/<major.minor>-template.conf  reference templates (registered in
                                             index; loadable from the debug page,
                                             never matched automatically)
filesDir/offsets.conf                     parsed/imported offsets (imported, HOCON)
internal overrides (sparse HOCON text)    written by the advanced override page
        │
        ▼  ProfileConfigController.resolve (Kotlin)
   resolved profile (one object, every field merged)
        │
        ├──▶ runtime: typed binary (direct and Shizuku both write it to native's stdin)
        │      native only parses and uses the geometry; it validates nothing
        ├──▶ snapshot: filesDir/<release>.conf (HOCON; the source for exports)
        └──▶ UI: override page / advanced override tree
```

Merge priority (low to high): `execution-tuning` (plus the per-route
`execution-<route>.conf`) → built-in profile + imported offsets → advanced
overrides. Finally `execution.selected_cpus` is forced in (from the manual
selection or an explicit imported/override value).

## 2. Top-level structure

```hocon
# GhostLock kernel profile (HOCON; JSON stays valid)
release = "6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k"
schema_version = 1
kernel_major = 6
route {
  select_stack { waiter_shift = -2 }
}
fallback { to = "none" }
kernelsnitch { collisions = 4, mm_struct_sz = 4096 }
task_struct { prio = 132, cred = 2080, pi_lock = 2316 }
cred { copy_size = 136, caps_offset = 48, caps_count = 5 }
offset { init_task = 34464384, init_cred = 34538824 }
# execution tuning is provided by the resolver from the preset files; write only differences
```

| Field | Type | Meaning |
|---|---|---|
| `release` | string | Must match the device's `uname -r` exactly (case and suffix); template files never participate in matching |
| `schema_version` | int | Always `1` |
| `kernel_major` | int | `5` or `6`; used for address resolution and sanity checks, no longer selects the route |
| `route` | object | Explicit route parent with exactly one branch; see section 3 |
| `fallback` | object | Fallback declaration (`to` plus an optional `route` branch); see section 3 |
| geometry | object/int | Grouped by namespace: `task_struct` / `cred` / `offset` / `kernelsnitch`. Omit unused route-specific fields entirely — don't write `0` or placeholders. `null` only appears in templates and in-progress edits |
| `execution` | object | Advisory tuning. General values come from `execution-tuning.conf`, per-route values from `execution-<route>.conf`; the resolver loads both as presets, device profiles don't `include` them. Write only differences |

## 3. Route mechanism

The route is no longer inferred from the kernel version or from which fields
exist: the profile declares it explicitly. `route` is a parent with **exactly
one branch**:

```hocon
route { tcp_zerocopy { compact_waiter = 1 } }
route { select_stack { waiter_shift = -2 } }
route { multicast_waiter { waiter_off = 96, buffer_size = 264 } }
```

- When an old config has no `route`, a compatibility inference runs:
  `kernel_major == 5 && mcast.waiter_off > 0` → `multicast_waiter`; otherwise
  `compact_waiter != 0` → `tcp_zerocopy`; otherwise `select_stack`. Importing an
  old `offsets.json` normalizes it to the new structure automatically.
- Each route needs only its own branch; write no other branch, and never leave
  two branches side by side (one config, one path).
- `fallback` declares a fallback: `"to"` is `"none"` or a route name. When you
  declare a target, also provide the `"route"` branch holding the fields the
  fallback needs:
  ```hocon
  fallback {
    to = "select_stack"
    route { select_stack { waiter_shift = 1 } }
  }
  ```
  The current implementation supports falling back from `tcp_zerocopy` to
  `select_stack`; write `"to": "none"` to disable it.
- Native's `RouteKind` enum maps one-to-one to the Kotlin values
  (`profile.h` / `ProfileConfig.Routes`).

### 3.1 Compatibility conversion for remote/main-era offsets

Upstream (the tip of `remote/main`, commit `bddfea46`) had only 6.x kernels, no
Shizuku path, and no 5.x geometry; its `offsets.json` entries are the extractor
report format (`symbols` / `struct_fields` plus four top-level scalars and
metadata). `LegacyProfileConverter` normalizes them on load (idempotently):

| Upstream content | Conversion result |
|---|---|
| `symbols` object (`off_*` keys) | `offset.*` namespace |
| `struct_fields` object (`task_*` keys) | `task_struct.*` namespace; BTE-specific keys such as `rt_mutex_waiter` / `cred_uid` / `seccomp_*` stay in their original group and take no part in the new validation or comparison |
| top-level `pselect_waiter_shift` | `route.select_stack.waiter_shift`; for a tcp profile it stays as `fallback.route.select_stack.waiter_shift` |
| top-level `compact_waiter` / `mm_struct_sz` | `route.tcp_zerocopy.compact_waiter` / `kernelsnitch.mm_struct_sz` |
| `kimage_text_base` / `btf_size` / `kallsyms` | dropped |
| no `route` field | inferred from 6.x geometry: `compact_waiter` → tcp, otherwise select |
| no cred template | never invented; the built-in profile provides it |

An upstream document can never select the 5.x branch (`multicast_waiter` is
retained only as a guarded inference of the local format).

### Required-field matrix

| Field group | common (all routes) | tcp_zerocopy | select_stack | multicast_waiter |
|---|:---:|:---:|:---:|:---:|
| `offset.init_task` / `offset.init_cred` / `offset.root_task_group` / `offset.selinux_enforcing` | required | | | |
| `task_struct.prio` / `task_struct.pi_lock` / `task_struct.pi_waiters` / `task_struct.pi_blocked_on` / `task_struct.cred` / `task_struct.seccomp` | required | | | |
| `kernel_major` ∈ {5,6}, `cred.copy_size`, `cred.caps_count`, and the credential-template bounds | required | | | |
| `route.tcp_zerocopy.compact_waiter` / `route.multicast_waiter.compact_waiter` | | required | | required |
| `route.select_stack.waiter_shift` | | | required (0 is valid) | |
| `route.multicast_waiter.waiter_off` (>0), `route.multicast_waiter.buffer_size`, `route.multicast_waiter.task_offset`, `route.multicast_waiter.lock_offset`, `route.multicast_waiter.fake_lock_offset`, `route.multicast_waiter.fake_task_offset`, `route.multicast_waiter.lock_slots_offset`, `route.multicast_waiter.lock_slot_count`, `route.multicast_waiter.lock_slot_stride`, `offset.mcast_fake_bss`, `offset.empty_zero_page`, `kernelsnitch.mm_struct_sz`, `cred.ref_count` (>0) | | | | required |

Credential-template bounds (general): `cred.usage_offset + 4 ≤ cred.copy_size`;
`cred.caps_offset + cred.caps_count × 8 ≤ cred.copy_size`; `cred.ref_count ≤ 4`;
each `cred.refN_image` must be non-zero and
`cred.refN_offset + 8 ≤ cred.copy_size`. `multicast_waiter` additionally requires
`cred.copy_size ≥ 0xa0` and
`route.multicast_waiter.waiter_off + route.multicast_waiter.lock_offset + 8 ≤ route.multicast_waiter.buffer_size`.

## 4. Geometry field groups

### 4.0 Kernel objects and field mapping

Configuration field names are the kernel struct member names (offsets inside the
struct):

```
task_struct
├── prio             → task_struct.prio
├── normal_prio      → task_struct.normal_prio
├── sched_task_group → task_struct.sched_task_group
├── pi_lock          → task_struct.pi_lock
├── pi_waiters       → task_struct.pi_waiters
├── pi_top_task      → task_struct.pi_top_task
├── pi_blocked_on    → task_struct.pi_blocked_on
├── pid / tgid       → task_struct.pid / task_struct.tgid
├── atomic_flags     → task_struct.atomic_flags
├── real_cred        → task_struct.real_cred
├── cred             → task_struct.cred
├── comm / tasks     → task_struct.comm / task_struct.tasks
└── seccomp          → task_struct.seccomp
```

```
cred
├── usage            → cred.usage_offset / cred.usage_value
├── cap_*            → cred.caps_offset / cred.caps_count / cred.caps_value
└── reference repairs → cred.ref_count + cred.refN_offset / cred.refN_image
```

```
forged multicast object (multicast_waiter route)
├── waiter start     → route.multicast_waiter.waiter_off
├── task / lock      → route.multicast_waiter.task_offset / lock_offset
├── fake_lock / task → route.multicast_waiter.fake_lock_offset / fake_task_offset
└── lock slots       → route.multicast_waiter.lock_slots_offset / count / stride
```

The `offset` namespace holds kernel-image symbol offsets (`init_task`,
`init_cred`, `empty_zero_page`, …); `offset.slide_*` are the KASLR slide anchors.
Together with `kernel_phys_load` they turn symbol addresses into runtime
addresses.

### 4.1 Task structure offsets (`task_struct`)

| Field | Meaning |
|---|---|
| `task_struct.prio` / `task_struct.normal_prio` | Task priority / normal priority (PI boost check) |
| `task_struct.sched_task_group` | `sched_task_group` offset |
| `task_struct.pi_lock` / `task_struct.pi_waiters` / `task_struct.pi_top_task` / `task_struct.pi_blocked_on` | `pi_lock`, `pi_waiters`, `pi_top_task`, `pi_blocked_on` |
| `task_struct.pid` / `task_struct.tgid` | PID / TGID |
| `task_struct.atomic_flags` | `atomic_flags` (used to decide whether state was cleaned up) |
| `task_struct.real_cred` / `task_struct.cred` | real cred / cred pointers |
| `task_struct.comm` / `task_struct.tasks` / `task_struct.seccomp` | `comm`, task list, `seccomp` |

### 4.2 Credential template (`cred`)

| Field | Meaning |
|---|---|
| `cred.copy_size` | Total bytes copied from the credential struct |
| `cred.usage_offset` / `cred.usage_value` | Refcount field offset / target value |
| `cred.caps_offset` / `cred.caps_count` / `cred.caps_value` | Capability set offset / count / fill value |
| `cred.ref_count` | Number of reference fields to repair (≤4) |
| `cred.refN_offset` / `cred.refN_image` (N=0..3) | Offset of each reference field / image value to restore |

### 4.3 Kernel symbols and slide anchors (`offset`)

| Field | Meaning |
|---|---|
| `offset.init_task` / `offset.init_cred` | `init_task` / `init_cred` offsets relative to the kernel image base |
| `offset.root_task_group` | `root_task_group` offset |
| `offset.selinux_enforcing` | `selinux_state.enforcing` offset (W1 writes 0) |
| `offset.selinux_blob_sizes` / `offset.security_hook_heads` | SELinux / security-hook offsets |
| `offset.slide_nfulnl_logger` / `offset.slide_boot_id` / `offset.slide_loggers_0_1` | KASLR slide anchors |
| `kernel_phys_load` | Kernel physical load address (0 falls back to the SoC formula) |
| `recommend_shizuku` | Whether this kernel recommends the Shizuku path (0/1, required in every profile, default 0; advisory only). It is not shown in any editor: for recommended kernels the app **turns the home-screen "Run via Shizuku" switch on at every start**. You can turn it off for the session, and once off the app stops requiring Shizuku for that session |

### 4.4 select_stack / tcp route fields

| Field | Meaning |
|---|---|
| `route.select_stack.waiter_shift` | Relative shift of the select-route waiter on the stack (0 is valid); under a fallback declaration this is `fallback.route.select_stack.waiter_shift` |
| `route.tcp_zerocopy.compact_waiter` | Compact-waiter layout flag for the tcp route; the multicast branch needs it too (`route.multicast_waiter.compact_waiter`) |

### 4.5 multicast_waiter route fields (`route.multicast_waiter`)

| Field | Meaning |
|---|---|
| `route.multicast_waiter.waiter_off` | Offset of the waiter in the multicast buffer (must be > 0) |
| `route.multicast_waiter.buffer_size` | Forged buffer size |
| `route.multicast_waiter.task_offset` / `route.multicast_waiter.lock_offset` | Task / lock field offsets in the buffer |
| `route.multicast_waiter.fake_lock_offset` / `route.multicast_waiter.fake_task_offset` | Forged lock / task object offsets |
| `route.multicast_waiter.lock_slots_offset` / `route.multicast_waiter.lock_slot_count` / `route.multicast_waiter.lock_slot_stride` | Lock-slot array offset / count / stride |
| `offset.mcast_fake_bss` | Kernel-image offset of the forged BSS page |
| `offset.empty_zero_page` | `empty_zero_page` offset |

### 4.6 KernelSnitch values (`kernelsnitch`)

Shared by every route (KernelSnitch drives the `mm_struct` leak search), not
affected by route choice:

| Field | Meaning |
|---|---|
| `kernelsnitch.collisions` | Number of futex collisions needed |
| `kernelsnitch.mm_struct_sz` | SLUB size of `mm_struct` (falls back to the built-in default when omitted) |

> Old flat keys (`kernelsnitch_collisions` / `mm_struct_sz` / `task_*` /
> `cred_*` / `off_*` / `mcast_*`) are folded into the matching namespaces when
> importing an old `offsets.json`, parsing extractor output, or reading advanced
> overrides.
> Internal storage (`offsets.conf`, advanced overrides, snapshots) and exports
> are all HOCON; old JSON caches are **not migrated and are discarded at
> startup**.

## 5. execution tuning (advisory)

Every `execution` value is advisory. The app merges them into the profile and
passes the result to native; see [defaults.md](defaults.md) for semantics and
defaults. The common groups come from `execution-tuning.conf` and the per-route
groups from `execution-<route>.conf`; the app resolver (and the Gradle exporter)
load these presets directly, so device profiles no longer `include` them:

- `recommended_cpus` / `selected_cpus`: suggested and locally selected cores
  (`selected_cpus` is maintained by the "general parameter override" page or the
  home-screen CPU picker)
- `heap`: KernelSnitch search attempt counts and timeouts
- `race`: route race wait / settle / poll intervals
- `stages`: W1/W2/W3 attempt counts and settle times
- `routes.tcp_zerocopy` / `routes.select_stack` / `routes.multicast_waiter`:
  per-route retry and wait parameters, in `execution-tcp-zerocopy.conf` /
  `execution-select-stack.conf` / `execution-multicast-waiter.conf`. When
  composing the native document, Kotlin fills in missing route groups with the
  defaults, so native always receives a complete `routes` object
- `handoff`: root handoff and KernelSU load polling

## 6. Validation and feedback

Validation happens in Kotlin (`AndroidProfileConfigController.validateProfileFields`):

1. It checks the common fields and the selected route against the matrix in
   section 3. Missing (`null`) or zero required fields and out-of-range
   combinations (credential / multicast bounds) are recorded in
   `ProfileConfig.invalidPaths`.
2. On the override pages, invalid entries are shown with a red label (unfilled
   counts as invalid); overridden and valid entries are yellow.
3. `fallback.to` must be `"none"` or a valid route name. When a fallback is
   declared, the target branch's required fields are validated as well (for
   example, a `select_stack` fallback needs
   `fallback.route.select_stack.waiter_shift`; 0 is valid).
4. The home-screen **Run** button is disabled while `invalidPaths` is non-empty;
   tapping it asks you to fix the red entries. Even if you bypass that,
   `runExploit` blocks before starting native and writes to the log.
5. Native no longer validates geometry; it only parses the v3 binary (v2 is
   still decoded) and executes the component selection and fields it was given.

## 7. Load layers and storage locations

| Layer | Source | Location | Written by |
|---|---|---|---|
| shared | shared values; the resolver/exporter load the tuning presets, device profiles `include` the core ones | `execution-tuning.conf` / `execution-<route>.conf` / `credential-6x.conf` / `kernelsnitch-6x.conf` | shipped with the app |
| builtin | exact `uname -r` match; no match means unsupported | `assets/kernel_profiles/*.conf` | shipped with the app |
| imported | parsed/imported offsets (same release entry) | `filesDir/offsets.conf` | Parse OTA / import config |
| general override | `execution.*` | the release entry in `filesDir/offsets.conf` | override page "general parameter override / reset" |
| route override | `route` / `fallback.to` | the release entry in `filesDir/offsets.conf` | advanced override page "route / fallback" |
| advanced override | any numeric path (sparse HOCON text) | internal `debug_profile_overrides` | advanced override (auto-saved) |
| snapshot | the fully merged HOCON | `filesDir/<release>.conf` | after any override is saved, before export |
| manual built-in source | another real release (dangerous) | internal `debug_builtin_release` | parameter page "load another built-in (dangerous)"; overrides stay bound to the device release |

Export: writes the merged snapshot for that release (one HOCON file, includes
already merged, tuning trimmed to the selected routes) into a folder you pick
via SAF.

## 8. Template profiles (`*-template`)

- One per kernel family: `5.15-template` / `6.1-template` / `6.6-template` /
  `6.12-template`, registered in `index.conf`, strictly for **development and
  debugging reference**.
- Every geometry/offset field is `null` (unfilled) and `route` / `fallback`
  show the full branch structure; each field has a Chinese comment you can copy
  from.
- The built-in picker's "templates (reference, unfilled)" section can load them
  to inspect the field structure. Templates **never participate in device
  matching and never auto-fall-back**: a device with no exact match and no
  imported offsets is treated as unsupported.
- A template carries only core fields (plus the core `include`s); execution
  tuning is provided by the resolver from `execution-*.conf`, so copying a
  template needs no tuning `include`. Synchronized copies live in
  `docs/kernel_profiles/templates/`; the originals under assets can be viewed
  with adb.

## 9. Native transport and parsing

Runtime configuration crosses as a **typed binary struct** (v3), no longer JSON
text. v2 documents are still decoded for compatibility.

- Kotlin serializes it from `NativeProfileDocument` (a data class matching
  native's `struct kernel_offsets`) via `toBinaryV3()`:
  `u32 magic(0x0D000721) + u16 version(3) + u16 frontend_id + u16 backend_id + u16 middleware_id + u8 kernel_major + u8 fallback_route + u16 release_length + release + 68×u64 core slots + u16 middleware entry count + N×(u8 key_length + key + i64) + u16 option count + M×(u8 key_length + key + i64)`, little-endian. `middleware_id` carries the route; `recommend_shizuku` is App-only and absent from the wire. The field order lives in `NativeProfile.kt` (`flattenCommon` / per-route `RouteConfig`) and `profile/binary.cpp` (`kCommonFields` / `kTcp/kSelect/kMulticastFields`); the two sides must change together.
- Transport path: direct and Shizuku both hand the profile to native on
  **stdin** (`--ghostlock-app-call`); nothing is written to `active-profile.bin`
  and `--profile` no longer exists.
- Native has exactly one decode path: `profile/entry.cpp` hands the stdin (or
  file) bytes to `profile/binary.cpp::parse`. It detects no format other than
  the magic and has no JSON fallback — v1 JSON is parsed only during import
  conversion (`legacy/offsets_json.cpp`).
- Internal storage and "export config" are HOCON (human-readable); JSON only
  appears in the v1 conversion (old `offsets.json`, extractor output).
- Runtime route and capability decisions (`TargetProfile::route()`,
  `TargetProfile::supports()`, `route_capability`) are all based on the decoded
  route.

## 10. Checklist for changing configuration

1. Change only the fields you need; omit unused route fields entirely (don't add
   `0` placeholders).
2. `route` has exactly one branch, and that branch must carry the route's
   required fields. When `fallback.to` names a target, fill the
   `fallback.route` branch the same way.
   Pull shared core values in with `include` instead of copying:
   `credential-6x.conf` (6.x credential template) and `kernelsnitch-6x.conf`
   (6.x collisions). Execution tuning (`execution-tuning.conf` /
   `execution-<route>.conf`) is loaded by the resolver as a preset; don't
   `include` it in a device profile.
3. Change `execution` only with device measurements; otherwise keep the
   defaults.
4. Verify locally: `make native-host-tests` (profile decode/validation vectors)
   and `./gradlew :app:assembleDebug`.
5. When renaming or regrouping fields, update `FieldLabels.kt` +
   `values*/strings.xml` and, when relevant, `docs/kernel_profiles/defaults*.md`.
