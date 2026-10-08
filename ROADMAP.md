# GhostLock Roadmap

> [中文版 / Chinese version](ROADMAP_ZH.md) · **Master plan (single progress entry):** [docs/plan/MASTER-PLAN.md](docs/plan/MASTER-PLAN.md)
>
> What follows is a statement of direction, **not a schedule**. Baselines: compared with `origin/main` (`3d4306c1`), this branch carries **689 commits, 1430 files changed, ≈ +201k lines**. Every batch, this page and the master plan get updated.

## 0. In one sentence

Take GhostLock from "three attack routes that work" to a **composable, programmable, verifiable** kernel privilege-escalation runtime: configuration expressed in Lua, native side carried by contract-based backends, handoff and plugins both extensible — **without diluting the security boundary or the verification discipline**.

## 1. Already landed (relative to `main`; every row has code and gate evidence)

| Area | Status |
|---|---|
| **New backend: CVE-2026-43284** | Page-cache write + vendor `insmod` trigger → resident LKM registers `/dev/glk` → UMH execution. It **coexists** with the 43499 PI-futex chain, sharing one document and one Pipeline (not two entry points). |
| **Config model convergence** | The user-facing selection changed from a **token list** to a **step queue** (`backend.<id>.route` + `queue`); the wire format is GLKv3 (MessagePack, `schema == 3`, canonical = shortest integer + UTF-8 key order); the Kotlin side uses a single model with one read entry point. |
| **Custom handoff unfrozen** | From pre-baked tokens to a **declarable selection surface** (route × queue × target program), wired into the `available` declaration and greyed-out UI. |
| **vrko countermeasure removal** | The **configuration face of `vr_guard` is deleted** (commit `b55708a8`): nothing writes the field, the related hooks are provably no-ops, and the attack path is unchanged. **Removing the code face** waits for device gating — we do not touch the attack path without device verification. |
| **Plugin system P1** | Import (no-backup + local SHA-256 + probe) → descriptor-driven validation → emit only when `enabled=true`; the runtime "load → call per stage → unload" path is wired. **Still paused by user instruction**; the new shape is described below. |
| **Engineering system** | Single-authority + end-to-end agreement tests (combination / vocabulary / stepset / manifest / golden / fixture, byte-for-byte), the R1 include firewall (**empty ledger + pin**), **164 archived device-gate records**, and a **design & planning review role** (design-critic) with review principles v0.2. |

## 2. What is next (ordered by dependency)

### 2.1 Native rewrite: from "one chain" to "a runtime"

- **Contract-ization**: separate *what is written* (primitives) from *how it is written* (routes) behind stable contract surfaces; backends implement primitives, routes are invoked as paths, and the assembly layer depends only on contracts.
- **Primitive registry + pre-flight gate**: every operation gets a declarative spec (parameters, allowed routes, capability requirements); the plan is judged once before start-up and **unsupported combinations are refused up front** (named reason, no runtime fallback).
- **One authoritative document**: load only the set of backends/routes the plan actually uses; read every field through exactly one entry point.
- **PI-window boundary discipline**: *outside* the window, virtual interfaces and dynamic dispatch are fine; *inside* it, only concrete types + static calls + POD are allowed (no indirect dispatch, no allocation, no first touch of a new page). This is not a slogan — it has executable criteria.

### 2.2 Lua configuration: a Turing-complete orchestration layer

- **Scripts at runtime**: Lua 5.4 statically linked (measured ≈ 107 KB, ≈ 2.2% of the current binary); scripts are parsed **before the attack** and executed **between operations**.
- **Sandbox**: `io`/`os`/`package`/`debug`/`load` are not registered; instruction budget (`lua_sethook`) + memory cap (`lua_setallocf`) + a per-script size limit.
- **Typed handles**: scripts only ever hold a handle of "kind + index"; **no raw pointers or raw addresses are exposed**, so a malicious or buggy script cannot invent an address to drive a write primitive.
- **Trust model**: import is **unrestricted** (the community can share optimized attack paths); provenance and hash are displayed but **not blocked**. The real boundary is the sandbox plus the capability surface — **no block-device or arbitrary-path write primitive is on the whitelist**, so scripts cannot reach AVB/partitions through capabilities.
- **Division of labour**: static policy and structure live in documents; dynamic parameters and conditional orchestration live in scripts.

### 2.3 Deeper custom handoff

- **Payload as steps**: commands/scripts and module loading are all steps in the queue and can be combined freely; **unsupported combinations are refused before start-up**.
- **Execution list**: scripts are expressed as a list (no more multiple blocks) and the count is **not capped** (lists can be concatenated); an entry marked `always` runs unconditionally (used for cleanup).
- **One prebuilt script**: the same script runs whether execution is user-space or kernel-initiated; the final `.sh` is synthesized by native from a built-in template plus the user fragment before the attack.
- **Three-layer cleanup guarantee**: template tail (in-process) → `always` entries (executor-guaranteed) → kernel fallback (module self-unload / SELinux restore parameters).
- **One executor**: UserRoot / Umh / CredGrant / KernelCmdSet — **chosen at build time**; failure ends the run (no runtime fallback).

### 2.4 Plugin system (after unfreeze)

- **Control plane in Lua**: shares the same VM, sandbox and budget as payload; each plugin gets its own environment sandbox and its own budget account.
- **Descriptor = a manifest table returned by the script**: capability bits, stage, parameter schema, version, hash — validated at load time.
- **Kernel hot paths stay native (.so / LKM)**: parts that need dense memory operations or direct kernel interaction keep the native channel.
- **The old single-channel `.so` design will not return**: plugins become two layers — restricted script + kernel-type module.

## 3. How we deliver (no hand-waving here)

1. **Batches**: B0.1 concepts and layer ownership → B0.2 capability-layer wiring → B0.3 per-entry route selection → B1 primitive registry and gate → B2 Lua integration → B3 plugin shape. The next batch starts only after the previous one verifies — this is a **cadence, not a calendar**: no time windows are promised.
2. **Criteria per batch**: black-box (commands, document bytes, logs, marker files) + white-box (unit tests, static assertions, layer tables, ledgers); only extreme inputs are tested, and impossible cases are not written.
3. **Attack-path changes = device gating**: cold device, fixed CPU pair, one route, clean boot; results are archived in the device-gates format. Disassembly comparison is an optional diagnostic, not a gate.
4. **Design before code**: L-level changes need a design document and a **non-author** review (principles role + native + config + docs) before any code is written.
5. **Honesty first**: what cannot be done is recorded as technical debt with a repayment trigger; superseded proposals are **struck through and kept** — history is not deleted.

## 4. What you can see soon

- The design family (15 documents + 2 ADRs) and the **master plan** are already in the repository: [docs/plan/MASTER-PLAN.md](docs/plan/MASTER-PLAN.md).
- Batches B0.1/B0.2 do not touch the attack path and **need no device**, so the community can build and verify them anywhere.
- B2 (Lua integration) lands the **sandbox and budget** first, then the script API; script examples and the list of refusal reasons will follow.

## 5. Want to help?

- **Devices / profiles**: the extractor emits `schema 3` HOCON; for a new device, include `uname -r` and the geometry source.
- **Device reports**: attach the log directory (`Download/ghostlock-debug-log/<time>`) and the effective configuration; we archive them in the gate format.
- **Plugin authors**: the descriptor format and stage rules live in the ADRs and the plugin design documents; the Lua control-plane draft is settled and interface feedback is welcome.
- **Challenge us**: this project welcomes **evidence-backed disagreement**; every conclusion in the design documents carries a `file:line` or a measured number, so please check them.

---

Glossary — **route**: the concrete path a write primitive takes (`tcp_zerocopy` / `select_stack` / `multicast_waiter`); **backend**: a vulnerability chain (43499 / 43284); **handoff**: how root is handed over after escalation (executor + script + cleanup); **PI window**: the genuinely dangerous stretch of the PI-futex race.
