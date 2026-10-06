# Kernel Profile Structure (Configuration System)

> **Status: the HOCON refactor landed in native on 2026-10-05** (step ① `b55708a8`: root scalar channel plus removal of the `common` and `countermeasure` owners and vr_guard phase (b); steps ②③④ `23958eb0`: `platform.abi.*` → `backend.cve_2026_43499.abi.*` in 62 places, the 43284 tuning keys into `backend.cve_2026_43284.execution.*`, the new `wire_only` marker and a 114-line manifest). **The App side (tests + golden) is still following** ⇒ cross-end status is "native final, App in progress". The old `common` / `platform` / `selection` layout is **deleted** (appearing ⇒ rejected).
> The old `common` / `platform` / `selection` layout is **deleted** (appearing ⇒ rejected). This document is the structure and flow authority; the field-by-field authority stays `profile-manifest-v3.tsv` (native-exported, two byte-identical copies).

## 0. Versions and field authority

- One number for everything: HOCON `schema_version = 3` and the GLKv3 wire `schema == 3` are the **same 3**. Do not add layered versions.
- **Only migration point**: Kotlin `LegacyProfileConverter` (legacy `1`/missing key → 3 with a diagnostic; any other value is rejected).
- native accepts only 3; wire v2 is deleted; the extractor only emits 3 (`--format conf` emits the **new shape** in the same batch).
- `kernel_profiles-legacy/*.conf` are **v1 input fixtures** and keep the **old shape** on purpose.
- Field authority: `make -C src profile-manifest-v3` → `profile-manifest-v3.tsv` (columns `owner / path / wire / required / default / source / doc`).
- **How to look up a field** (do not copy the authority into prose): `grep '^cve_2026_43499' app/src/test/resources/profile-manifest-v3.tsv` (or `^countermeasure`, `^backend`), and regenerate with `make -C src profile-manifest-v3` (two byte-identical copies). This document intentionally lists **structure, migrations and gates**, not field enumerations. Note the manifest `required` column means **unconditionally required**; per-tier/conditional requirements belong to the validator, never to that column.
- **Root scalars travel through the "root section"** (`document.hpp`'s `kRootSection`, an empty section name) so that the owner bind's single lookup path `find_value(section, key)` is isomorphic with root keys - the two section-copy filter copies cannot silently miss one (a miss would mean a silent `kernel_major = 0`). **Do not change this back to named members**.
- **`wire_only` FieldSpecs** (`kmi`, `lkm_path`, `carrier_path`): accepted on the wire and decoded, but **not in the manifest** — so the App's writable surface excludes them. Key distinction: **the manifest is the profile *writable* surface, not the wire surface**; the wire surface is defined by native's decode/bind path.
- **Evidence (2026-10-05)**: manifest **114 lines** (10 header + **104 fields**), two byte-identical copies at sha256 **`68bd7a506a210077`**, bare run `ok (104 fields, both copies)`, zero hits for the three wire-only keys; gates host `EXIT=0` (9-warning baseline, 58 tests, firewall `180/4/4/0/0`), lint 0, NDK 0; six negative cases at `src/core/tests/profile_v3_test.cpp:195-242` (`common.*`, `countermeasure.*`, in-section `kernel_major`, old `platform.abi.*`, old flat 43284 keys ⇒ rejected).

## 1. Data flow

```
assets/kernel_profiles/*.conf  (HOCON, new shape)
   └─ Kotlin: parse → normalize → merge (includes) → resolve
        ├─ App UI: pick backend from `available` ∩ native catalog, then a token from that backend list
        └─ Glkv3Encoder → [4B len][GLKv3 document][session frame] → native stdin
                                                    (--ghostlock-app-call)
native: frame_v3 (schema==3) → Document → owner/root-key gate → SchemaRegistry bind → Pipeline
```

The profile **declares what is available**; the **runtime choice** is the user/App, written into the wire as `backend.<id>.steps`.

## 2. File format (HOCON)

- HOCON supports comments, `${variables}` and `include`; JSON is valid HOCON.
- Root must be a map and must contain the `ghostlock { … }` section (GLKv3 document root).
- **Root-level scalars are a closed whitelist**: `schema_version`, `release`, `kernel_major`, `kernel_minor`, `safe_mode`.
- `include`d fragments carry no `schema_version`.

## 3. Canonical layout (new shape)

```hocon
ghostlock {
  schema_version = 3
  release        = "<exact uname -r>"
  kernel_major   = null            # kept for future use
  kernel_minor   = null            # added by the refactor
  safe_mode      = false

  available {                      # two levels: backend key → token list
    cve_2026_43499 = [ "mcast_rootchild", "pselect_rootchild" ]
    cve_2026_43284 = [ "umh" ]
  }

  backend {
    cve_2026_43499 {
      steps = "mcast_rootchild"    # chosen at runtime; wire carries it
      abi { task_struct { … } cred { … } kernel { } offset { … } }   # was platform.abi.*
      route { } cred { } kernel { } offset { } execution { }
    }
    cve_2026_43284 {
      steps = "umh"
      execution {                  # late_load_args / selinux_exec_context /
        …                          # module_poll_attempts / module_poll_interval_ms / wait_timeout_ms
      }
    }
  }

  # countermeasure { }            # REMOVED (user ruling 2026-10-05): the owner became empty
  #                               # after common.vr_guard and countermeasure.vivo_vr_guard.* were deleted
  #                               # (wire + manifest rows too) -> appearing means rejected
}
```

Owner set after the refactor: **`backend.<id>` only** (plus the root scalars and the root-level `available{}`). The `countermeasure.*` owner is **removed** because the vr_guard fields are gone: with no writer left, `vr_guard_enabled()` is constantly false and the two `VivoPluginPolicies::apply(...)` call sites in `steps.cpp` are provably no-ops, so the attack path is untouched. The `platform/vivo/**` code and those call sites **stay (inert)**; deleting them outright is phase (a), an attack-path change that needs a device gate and is queued for the first batch once a device is available.

## 4. Availability and selection

- `available { <backend> = [ tokens ] }` is **two levels**: pick a usable **backend** first, then a **combination token** under it.
- **`selection { backend, terminal }` is deleted** and **the `terminal` concept is removed from HOCON** — the token already implies the terminal (`*_rootchild` / `*_shizuku` / `umh`).
- The token vocabulary authority is `contract::kCombinationCatalog` (12 tokens = 7 wired + 5 planned). The App only offers tokens that are in both `available` and the native catalog, and the runtime choice is written to the wire as `backend.<id>.steps`.
- `index.conf` uses `usable = [{ id, usable }]` (build/asset-layer semantics) — deliberately a **different name** from the profile-level `ghostlock.available{}`.

## 5. Backends

**`cve_2026_43499`**
- `steps` — the combination token (selection axis).
- `abi.*` — task_struct / cred / kernel / offset facts; this is where `platform.abi.*` moved.
- `route` / `cred` / `kernel` / `offset` / `execution` — route-private and geometry groups as before.

**`cve_2026_43284`**
- `steps` stays at the top of the backend (selection axis); 43284 has no route axis, so the token is a bare path name (`umh`).
- `execution.*` — `late_load_args`, `selinux_exec_context`, `module_poll_attempts`, `module_poll_interval_ms`, `wait_timeout_ms`.
- **`kmi` / `lkm_path` / `carrier_path` are deleted from the profile**:
  - `kmi` is derived from `release` (`major*1000+minor`); a hand-written value used to fail closed as `KmiFieldMismatch` — now the key does not exist (appearing ⇒ rejected).
  - `lkm_path` / `carrier_path` / every `.ko` path is resolved inside the **GhostLock internal directory** and **computed at runtime, then injected into the wire**. The **wire fields stay**; the profile no longer carries them.

## 6. countermeasure (removed)

- The `countermeasure.*` owner is **removed** (commit `b55708a8`, user ruling 2026-10-05 phase (b)): `common.vr_guard` and `countermeasure.vivo_vr_guard.tracepoint_funcs` are deleted together with their **wire and manifest rows**, so the owner became empty and **appearing now means rejected**.
- `defex` is **already deleted** (commit `a68e2d5a`).
- `platform/vivo/**` (8 files / 473 lines) and the two `VivoPluginPolicies::apply(...)` call sites in `backend/cve_2026_43499/steps.cpp` **stay, inert** — with no writer they are provably no-ops, so the attack path does not change. Deleting them is **phase (a)**: an attack-path change requiring a device gate, queued for the first batch once a device is available.

## 7. Deleted owners and frozen features

| Item | Status |
|---|---|
| `common` owner | **Deleted** — its keys became root scalars or disappeared |
| `platform` owner | **Deleted** — `platform.abi.*` moved to `backend.cve_2026_43499.abi.*` |
| `selection { backend, terminal }` | **Deleted** — replaced by `available{}`; terminal removed from HOCON |
| `plugin` owner | **Literally commented out** per user order 2026-10-05 (code/tests kept; appearing ⇒ rejected); restore = undo comments + gates |
| `payload` owner | Same freeze — no `payload.*` emission; execution half stopped |
| `countermeasure.*` owner | **Removed** (`b55708a8`); appearing means rejected |
| defex | **Deleted** (commit `a68e2d5a`) |
| vivo VR guard | **Phase (b) done** (`b55708a8`: profile/wire/manifest rows removed); **phase (a) code deletion queued behind a device gate** — `platform/vivo/**` and the two call sites stay inert until then |

## 8. GLKv3 wire

- MessagePack document, root map, `schema == 3`, no magic/header; canonical = shortest integers + keys sorted by UTF-8 bytes; length-prefixed stdin framing.
- Runtime-injected fields (`kmi`, module/carrier paths) are written by native from computed values; the profile does not supply them.
- The runtime selection is carried by `backend.<id>.steps`; root `available` is **not** a wire section.
- Static policy goes into the document; runtime secrets/SPI/ports never do (they travel in the session frame and are zeroed after use).

## 9. Validation and diagnostics

Fail-closed (whole document rejected, never a silent downgrade):
- a deleted owner (`common`, `platform`, `selection`) or a currently commented owner (`plugin`, `payload`) appears;
- a root scalar outside the whitelist, or an unknown top-level owner (e.g. `plugins`, `payloads`, `root`);
- `available` names a token that is not in the catalog, or `steps` contradicts the chosen token;
- `schema_version != 3`, or the root is not a map;
- paths containing `..`, backslashes or control characters, or longer than 256 B; non-lowercase-hex `sha256`;
- a hand-written `kmi` (the key is gone; appearing ⇒ rejected).

## 10. Storage and load layers

- Built-in profiles: `app/src/main/assets/kernel_profiles/` (`index.conf` + one `<uname-r>.conf` per release + shared `execution-*.conf` / `credential-6x.conf` / `kernelsnitch-6x.conf`).
- `index.conf`: `schema_version`, `usable` (backend list) and `profiles` (release → file).
- Overrides live in the App override store; the canonical document is what the App emits.

## 11. Commands and agreement tests

```sh
make -C src profile-manifest-v3      # regenerate the field table (two copies)
make -C src combination-manifest     # token whitelist export
make -C src vocabulary-manifest      # component vocabulary export
make -C src native-host-tests        # host tests incl. manifest/agreement assertions
./gradlew :profile-core:test :app:testDebugUnitTest
```

## 12. Legacy import

- v1 `offsets.json` is converted **only** in Kotlin by `LegacyProfileConverter`; native never parses v1.
- `kernel_profiles-legacy/*.conf` keep the old shape as fixtures; they are not part of the new layout and must not be "fixed" to it.

## 13. Checklist for changing configuration

1. Add the field to the native schema first (source of truth), then regenerate `profile-manifest-v3.tsv`.
2. Update the Kotlin adapter/UI and this document (and `PROFILE_SCHEMA_ZH.md`) in the same batch.
3. Never reintroduce `common` / `platform` / `selection`; new state goes into the existing owners or root scalars.
4. Never write runtime-computed values (`kmi`, module/carrier paths) into profiles.
5. Run the verification recipe in `PROFILE_TEMPLATE.conf` and archive device gates when the attack path is touched.
