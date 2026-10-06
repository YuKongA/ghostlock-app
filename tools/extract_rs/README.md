# Extractor (`ghostlock-extract`)

boot.img / OTA / URL -> GLK profile. Rust crate, the third layer next to the
Android app (`app/`) and the native runtime (`src/core/`). The CLI is self-documenting
(`--help`).

## Output formats

| `--format` | what it writes |
|---|---|
| `conf` | the canonical HOCON profile in the **frozen shape**: root scalars (`schema_version` / `release` / `kernel_major` / `kernel_minor` / `safe_mode`), `available { <backend> = [ token ] }`, then `backend.<id> { steps, abi { task_struct, cred, kernel, offset }, execution { ... } }`. The `common` / `platform` / `selection` owners no longer exist. |
| `json` | the historical analysis report (v1); untouched by the HOCON refactor. |

`flatten_conf_values()` reads a rendered `conf` back onto **wire paths** — the same
view the App folds and the plugin R1 extract lookup match against. The profile
spelling **is** the wire spelling (native rename `23958eb0` / `b55708a8`): root scalars are
bare keys, the ABI block is `backend.cve_2026_43499.abi.*`, the 43284 tuning is
`backend.cve_2026_43284.execution.*`. There is deliberately **no path rewriting** here (the
transitional mapping was deleted), so a document written in the deleted `common` /
`platform` spelling keeps those legacy keys and matches nothing — it is rejected
upstream instead of being silently accepted.

## `--plugin-descriptor` is frozen

⏸ The plugin project is paused (user directive 2026-10-05) and native rejects the
`plugin` owner outright, so the flag **fails closed** (exit 2, named error) instead of
emitting a `plugin {}` block that could never be loaded. The implementation is kept
intact; the unfreeze list (file:line, 8 steps) lives in `src/main.rs` banner comments
and in `--help`.

## Test

    cargo test --release
