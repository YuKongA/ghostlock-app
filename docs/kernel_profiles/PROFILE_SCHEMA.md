# Kernel Profile Structure (Configuration System)

> 中文: [PROFILE_SCHEMA_ZH.md](PROFILE_SCHEMA_ZH.md)

This document is the reference for the kernel profiles under
`app/src/main/assets/kernel_profiles/`: the HOCON layout, the field groups, the
combination-token selection, the plugin section, the GLKv3 wire and the load /
validation pipeline.

> To port a new kernel, follow [README.md](README.md). Execution-tuning defaults
> are listed in [defaults.md](defaults.md). Diagrams are **not** repeated here: the
> single authority for the process structure (IPO / state machines / class /
> sequence) is [full-process-uml.md](../development/full-process-uml.md).

## 0. Versions and field authority

There is exactly **one version number**: `3`. HOCON profiles carry
`ghostlock.schema_version = 3`, and the wire document is a MessagePack root map
whose `schema` field must equal `3`. The number is never stacked or bumped per
file. The plugin C ABI is a **separate** counter: `GLK_ABI_VERSION = 1`
(append-only, `src/core/contract/abi/glk_contract_abi.h`).

| Fact | Authority |
|---|---|
| Canonical profile shape, key ownership, alias normalization | checked-in `app/src/main/assets/kernel_profiles/*.conf`; parser `profile-core/.../data/ProfileLayout.kt` |
| Owner-qualified path -> wire type (109 fields) | native export `app/src/test/resources/profile-manifest-v3.tsv` (test copy) and `profile-core/src/main/resources/profile-manifest-v3.tsv` (runtime copy); generator `make -C src profile-manifest-v3` |
| Selection vocabulary (12 combination tokens) | `contract::kCombinationCatalog` (`src/core/contract/identity.hpp`); export `make -C src combination-manifest` |
| Plugin wire shape and dynamic keys | `src/core/plugin/schema.hpp` (`kPluginGlkv3Fields`, line 95) and `src/core/plugin/wire.{hpp,cpp}` (`validate_plugin_wire`, `wire.hpp:66`) |
| Plugin C ABI version | `src/core/contract/abi/glk_contract_abi.h:47` (`GLK_ABI_VERSION 1u`) |
| Token catalogue | `src/core/contract/identity.hpp:177` (`kCombinationCatalog`, 12 rows) |
| Runtime index | `app/src/main/assets/kernel_profiles/index.conf:3` (`schema_version = 3`) |
| GLKv3 wire format | [wire-transport-model.md](../analysis/wire-transport-model.md) |
| Process structure diagrams | [full-process-uml.md](../development/full-process-uml.md) (single authority; this file links, never redraws) |

## 1. Data flow

1. **Load** the built-in profile for the device's exact `uname -r`, the shared
   `execution-*.conf` presets, imported/exported user profiles and the advanced
   overrides. Fragments are pulled in with `include`.
2. **Normalize** every document into the canonical owner-qualified layout
   (`ProfileLayout`): both the canonical and the older flat spelling are accepted,
   aliases are resolved, and an unrecognized key fails closed with its dotted path.
3. **Merge** (low to high): execution presets -> built-in + imported profiles ->
   advanced overrides; `execution.selected_cpus` is then forced from the pairing
   the user selected (`ProfileMerger`/`ProfileResolver`).
4. **Validate**: the release must match the device; schema-required fields must be
   present; defaults are materialized from the schema (a defaulted field is
   reported as `default_used`).
5. **Encode**: the resolved profile becomes a GLKv3 document (root map,
   `schema == 3`), canonically encoded (shortest integers, map keys sorted by
   UTF-8 byte order).
6. **Frame and hand off**: the app starts the native executable and writes the
   document to stdin with a 4-byte big-endian length prefix; a runtime-secret frame
   may follow the document on the same stream. Secrets never appear in the
   document, in argv or on disk.
7. **Parse and bind natively**: a non-map root, a missing or non-3 `schema`, an
   unknown section/key, a wrong type, a truncated document or a document over
   1 MiB is rejected before any attack stage runs.

## 2. File format (HOCON)

- HOCON is used everywhere: built-in profiles, `index.conf`, fragments, imports
  and exported profiles. JSON stays valid; `#` / `//` comments, trailing commas
  and `${var}` substitution (including `${?var}`) are accepted.
- `include "file.conf"` is supported (same directory, nestable, loop-safe).
  Included fragments carry **no** `schema_version` — only the profile root does.
- `index.conf` is the runtime index:

```hocon
schema_version = 3
backends = [
  { id = "cve_2026_43499", available = true }
  { id = "cve_2026_43284", available = true }
]
profiles = [
  { release = "6.12-template", file = "6.12-template.conf" }
  { release = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k", file = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k.conf" }
]
```

  The `backends` matrix is asserted against the native-exported manifest
  (`BackendMatrixAgreementTest`), so it cannot drift.

## 3. Canonical layout (owner-qualified)

Built-in profiles use a single wrapper root, `ghostlock`, with one section per
owner. The complete shape of a real profile:

```hocon
# GhostLock kernel profile (HOCON, canonical R3 owner-qualified layout).
ghostlock {
  include "credential-6x.conf"
  include "kernelsnitch-6x.conf"
  schema_version = 3
  release = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
  selection {
    backend  = "cve_2026_43499"     # owner of the steps token below
    terminal = "root_child"         # consistency check against the token
  }
  common {
    kernel_major = 6
  }
  platform {
    abi {
      task_struct { prio = 148, cred = 2304, comm = 2320 /* ... */ }
      offset { init_task = 37736192, init_cred = 37825128 /* ... */ }
      kernel { kernel_phys_load = null, kernel_phys_offset = null }
    }
  }
  backend {
    cve_2026_43499 {
      steps = "pselect_rootchild"   # the ONE user-visible selection token
      route { select_stack { waiter_shift = 0 } }
      offset { slide_loggers_0_1 = 37691640 /* ... */ }
    }
  }
}
```

Ownership by field count (native manifest, 109 rows):

| Owner section | Fields | Holds |
|---|---|---|
| `backend.cve_2026_43499` | 58 | the steps token, route geometry, credential template, KernelSnitch values, execution tuning |
| `platform.abi` | 31 | `task_struct`, ABI-level `offset`, `kernel_phys_*` |
| `backend.cve_2026_43284` | 10 | page-cache/LKM policy: module and carrier paths, handshake timeouts |
| `plugin.<id>` | 6 | the plugin section (4 static rows + 2 dynamic rows), see section 5 |
| `countermeasure.vivo_vr_guard` | 1 | vendor countermeasure parameters |
| `common` | 3 | `kernel_major`, `safe_mode`, `vr_guard` |

Counts are checkable:

```sh
awk -F'\t' '!/^#/{split($2,a,"."); print a[1]"."a[2]}' app/src/test/resources/profile-manifest-v3.tsv | sort | uniq -c
```

Rules:

- one fact, one owner: a key lives in the section of the component that consumes
  it; shared values live under `common`;
- `selection.backend` and `selection.terminal` are **consistency checks** — the
  selection itself is the token in `backend.<id>.steps`;
- unrecognized keys are rejected (fail-closed) with their dotted path; nothing is
  silently dropped;
- an older flat document is still *accepted at parse time* and normalized into this
  layout; new profiles must be written in the canonical layout.

## 4. Selection: one combination token

The user-visible selection is exactly **one token** stored in
`backend.<id>.steps`. The token derives the route, the step set and the
terminal; those three are no longer independently selectable.

| Available (7) | Planned (5) |
|---|---|
| `mcast_rootchild`, `pselect_rootchild`, `tcp_rootchild` | `mcast_umh`, `pselect_umh`, `tcp_umh` |
| `mcast_shizuku`, `pselect_shizuku`, `tcp_shizuku` | `rootchild`, `shizuku` (cve_2026_43284) |
| `umh` (cve_2026_43284) | |

- the owner prefix before `_` is the route: `mcast` = `multicast_waiter`,
  `pselect` = `select_stack`, `tcp` = `tcp_zerocopy`; a backend without a route
  axis (cve_2026_43284) uses the bare path name;
- planned tokens parse and are known, but the selection gate rejects them and the
  app greys them out;
- an unknown token is rejected with the token text echoed; a missing `steps` key
  is rejected;
- the root `route` / `terminal` values must agree with the token, otherwise the
  document is rejected;
- older documents that carried a numeric step id or a legacy step token are
  migrated to the equivalent combination token with a diagnostic on stderr.

## 5. Plugin section (P1)

`plugin` is a third top-level owner (neither a backend nor the platform), because
one countermeasure can serve several backends:

| Path | Type | Rule |
|---|---|---|
| `plugin.<id>.enabled` | bool | default false; **only `true` is emitted** |
| `plugin.<id>.stage` | str | one of the host stage tokens (`pre_spawn`, `post_spawn`, `pre_terminal`, `post_terminal`) |
| `plugin.<id>.module_path` | str | path relative to `<GHOSTLOCK_HOME>/countermeasures`; no absolute path, no `..`, no backslash |
| `plugin.<id>.module_hash` | str | 64 lower-case hex digits (SHA-256 of the module) |
| `plugin.<id>.params.<key>` | dynamic | value type comes from the plugin descriptor |
| `plugin.<id>.extract.<key>` | dynamic | produced by the extractor projection; the shape is validated here |

- the two dynamic paths are declared in the manifest with the **union** type
  `uint|int|bool|str`; the concrete type of a key is fixed by the loaded module's
  descriptor (read through the read-only native probe `--plugin-probe`), not by a
  static table;
- the document is fail-closed: an unknown field, a missing/non-bool/`false`
  `enabled`, an unknown stage, a bad module path or hash, an empty dynamic key and
  more than 16 plugins are all rejected before the attack runs, never silently
  dropped;
- there is **no** plugin asset file: plugin configuration is device/user specific
  and lives in the override store; the app writes `plugin.<id>.*` only for enabled
  plugins;
- the plugin C ABI is `GLK_ABI_VERSION = 1` and append-only; the probe prints a
  TSV description whose column order is frozen in
  [contract-design.md §3.14.7](../analysis/contract-design.md);
- **boundary (P1)**: declare -> validate -> bind is shipped. The runtime that loads
  the module and invokes it at its stage is **not wired yet** (tracked as task-9);
  the probe itself never registers or runs a hook.

## 6. Geometry field groups

Geometry is grouped by kernel object, all under owner sections:

| Group | Section | Fields (examples) |
|---|---|---|
| Task structure | `platform.abi.task_struct` | `prio`, `normal_prio`, `pi_lock`, `pi_waiters`, `pi_top_task`, `cred`, `comm`, `tasks`, `seccomp` |
| Kernel symbols / slide anchors | `platform.abi.offset` (ABI-level) and `backend.cve_2026_43499.offset` (route-specific) | `init_task`, `init_cred`, `selinux_enforcing`, `slide_loggers_0_1` |
| Physical mapping | `platform.abi.kernel` | `kernel_phys_load`, `kernel_phys_offset` |
| Credential template | `backend.cve_2026_43499.cred` | `copy_size`, `caps_offset`, `caps_count`, `caps_value` |
| KernelSnitch | `backend.cve_2026_43499.kernel` | `kernelsnitch_collisions`, `mm_struct_sz`, `compact_waiter` |
| Route geometry | `backend.cve_2026_43499.route.<route>` | `select_stack.waiter_shift`, multicast/TCP tuning |

Omit unused route-specific fields; do not write `0` or placeholders. `null`
appears only in half-filled templates and means "not derived yet". The exhaustive
path -> type list is the manifest (section 0).

## 7. Execution tuning (advisory)

Execution tuning is provided by the resolver from the shared fragments
(`execution-tuning.conf` plus `execution-<route>.conf`); device profiles include
them and override only differences. The values live under
`backend.cve_2026_43499.execution` (`recommended_cpus`, `heap`, `race`,
`tcp`/`select`, `handoff`). `execution.selected_cpus` is always re-derived from
the resolved pairing, so the device never depends on a stale value.

## 8. GLKv3 wire

- The document is a plain MessagePack value whose root is a **map**; `schema`
  must equal `3`. There is no magic, no version prefix and no separate header.
- Keys: `schema`, `release`, `backend`, `terminal`, and `sections` (a map of
  owner-qualified section names to maps of key -> value).
- Types: unsigned ints for offsets/lengths, signed ints where negative values are
  legitimate, bool (an explicit `false` differs from an absent key), UTF-8 str for
  tokens/paths, bin for byte blobs, array, map.
- Canonical encoding: shortest integer form, map keys sorted by UTF-8 byte order,
  no floats; the same logical document must be byte-identical every time.
- Presence is carried by key occurrence; an omitted field is not a zero.
- Rejection (fail-closed, before any stage): non-map root, missing `schema`,
  `schema != 3`, unknown section or key under the production schema, type
  mismatch, truncation, over-deep/over-large documents (1 MiB document limit).
- Transport: stdin of the native executable, a 4-byte big-endian length prefix
  followed by the document (and optionally a runtime-secret frame after it), or a
  prebuilt `.bin` file for debugging. Runtime secrets never enter the document.
- The extractor never writes the wire: it emits HOCON (`--format conf`,
  `schema_version = 3`) or the legacy JSON report.

## 9. Validation and diagnostics

- `release` must match the device's `uname -r` exactly (templates never match);
- schema-required fields must be present; a value materialized from a schema
  default is reported as `default_used` so a silent default is visible;
- a migrated legacy step id is reported on stderr;
- unknown keys, unknown tokens, unavailable (planned) combinations, a root
  route/terminal that disagrees with the token, and any plugin rule from section 5
  fail closed — the document is rejected instead of being partially applied.

## 10. Storage and load layers

| Layer | Location |
|---|---|
| Built-in profiles, `index.conf`, templates, fragments | `app/src/main/assets/kernel_profiles/` (read-only, shipped in the APK) |
| Imported / exported user profiles | app-private `user_profiles/` under the app files directory |
| Advanced overrides | the app's override store (per-field, applied last) |
| Plugin modules | app-private **no-backup** `countermeasures/` root (a module must never ride Android auto-backup) |
| Exported wire for debugging | `./gradlew exportKernelProfiles` -> `build/kernel-profiles/*.bin` |

## 11. Commands and agreement tests

```sh
make -C src profile-manifest-v3      # regenerate the path -> type manifest
make -C src combination-manifest     # regenerate the token manifest + resolve vectors
./gradlew exportKernelProfiles       # export GLKv3 .bin profiles
./gradlew :app:testDebugUnitTest :profile-core:test
```

Cross-language agreement is tested, not remembered: `ProfileManifestV3AgreementTest`
(Kotlin table == manifest), `BackendMatrixAgreementTest` (`index.conf` matrix ==
manifest), `CombinationTokenAgreementTest` / `CombinationTokenHardcodeTest`
(tokens come from the exported manifest, no hard-coded literals),
`ProfileLayoutEquivalenceTest` and `BuiltinProfilesTest` (every built-in profile
normalizes and validates), `PluginProbeGoldenTest` (the device probe golden) and
`LegacyProfileConverterTest` (the only migration point).

## 12. Legacy JSON import

The old `offsets.json` report can still be imported. Conversion happens **only**
in the app (`LegacyProfileConverter`, the single migration point) and produces the
current `schema_version = 3` HOCON; the native executable has no JSON or legacy
decoder. The same converter normalizes an older HOCON profile generation (a
deprecated version number, or the key missing) to `3` and records a diagnostic;
any other version value is rejected with the actual value in the message.

## 13. Checklist for changing configuration

1. Decide the owner section first; a new field goes where its consumer lives.
2. Add it natively (`FieldSpec`) and regenerate the manifest; never hand-edit the
   manifest.
3. If it selects behaviour, extend the token catalogue instead of adding a CLI flag
   or a second selection key.
4. Update the Kotlin side through the generated manifest, not with literals.
5. Add the field to the matching template/profile and keep fragments
   `schema_version`-free.
6. Wire a default in the schema if the field is optional; otherwise it must be
   required and fail closed when missing.
7. Run the gates: `make -C src native-host-tests`, the NDK build (zero warnings),
   `make -C src lint-tidy`, the Gradle tests, and the device gate when the change
   touches the attack path.
8. Update the single-authority diagrams in
   [full-process-uml.md](../development/full-process-uml.md) in the same batch when
   the structure changes.
