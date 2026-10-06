# Native core layout

## Components and pipeline

The execution chain is `Pipeline<Backend, Terminal>`, fixed at compile time:

- **terminal** - startup/handoff. Both `root_child` and `umh_forward` are
  available and device-verified. The root_child policy is declared by its owning
  backend (`backend/cve_2026_43499/terminal/root_child.hpp`, ADR-0006 T5); the
  neutral umh_forward policy and the shared inputs/probes stay in `terminal/`.
- **backend** - the vulnerability primitive and its write stages.
  `cve_2026_43499` is available; `cve_2026_43284` is wired but not
  device-verified; `cve_2026_64560` / `cve_2026_31431` / `cve_2026_43503` /
  `cve_2026_23274` are header-only unavailable placeholders.
- **route** - the backend-internal strategy realizing one write
  (`select_stack` / `tcp_zerocopy` / `multicast_waiter`). It is chosen by the
  backend from the resolved profile and is **not** an assembly axis.

The catalogue is a **token whitelist**: the composition authority is
`contract::kCombinationCatalog` (token -> {backend, route, steps, terminal,
available}), and the wired combinations are `cve_2026_43499` x
`{mcast,pselect,tcp}_{rootchild,shizuku}` plus `cve_2026_43284` x `umh`.
Planned entries (`{mcast,pselect,tcp}_umh`, `rootchild`, `shizuku`) are
registered with `available = false`: they parse but the selection gate rejects
them, echoing the token.

Where the pieces live:

- `contract/identity.hpp` - the stable component ids, per-axis availability
  (`backend_available` / `terminal_available` / `stepset_available`, with
  `selection_supported()` as the runtime fail-closed gate), the neutral terminal
  interface vocabulary (`TerminalInput` / `RootProgram` / `ActivationContext`)
  and the `BackendIdentity` / `BackendExecution<B, Input>` / `BackendState` /
  `TerminalIdentity` / `TerminalExecution` concepts with the identity registries
  (`for_each_backend`, `for_each_terminal`). It must stay host-compilable and
  must not include `backend/`, `pipeline/`, `platform/` or `terminal/`.
- `contract/identity.hpp` (`kCombinationCatalog`) - **the composition
authority**: the token whitelist with availability. `pipeline/component_catalog.hpp`
  only *dispatches* on it (`combination_supported()`, `DispatchTarget`,
  `dispatch_target_of()`, `path_target_of()`), and `Pipeline<Backend, ...>`
  static-asserts each wired combination.
- `pipeline/pipeline.hpp` - `Pipeline<Backend, Terminal>`: the only execution
  entry. It static-asserts the catalogued triple and both execution concepts,
  binds the optional backend State through RAII, exposes the dispatch `target`
  and returns `RunResult`.
- `pipeline/orchestrator.hpp` - `run_orchestrated_pipeline`: applies the
  `selection_supported()` gate, then switches on `dispatch_target()` and asserts
  each case against `Pipeline::target`.
- `backend/cve_2026_43499_backend.{hpp,cpp}` + `backend/cve_2026_43499/` - the
  available backend: `Cve2026_43499Policy` / `Cve43499_W1W2` over
  `Cve43499Primitives`, its `route/route_policy.hpp` middleware policies with
  the static route hooks (capabilities + `w2_fast_repair_*`), the per-route
  implementations under `route/`, the owner binding / GLKv3 schema in
  `backend_profile/` + `glkv3_schema.hpp` + `schema.hpp`, and the frozen
  upstream kernel-address discovery provider under `leak/` (relocated from
  `kernelsnitch/` in A3-2).
- `backend/cve_2026_43284/` - the wired-but-unavailable backend: `ipsec/`,
  `pagecache/`, `lkm/`, `steps/`, `backend_terminal.hpp`, `session_frame.*`,
  `glkv3_schema.hpp`, `schema.hpp`; `backend/cve_2026_43284_backend.hpp` holds
  `Cve2026_43284Policy`. The placeholder backends have one header each
  (`backend/cve_2026_64560_backend.hpp` etc.).
- `backend/victim/` - the victim child lifecycle and pipe context.
- `backend/cve_2026_43499/terminal/root_child.{hpp,cpp}` - the `RootChildPolicy`
  **declaration** and its 43499-specific handoff implementation, both owned by the
  backend (ADR-0006 T5 completed; the declaration moved out of `terminal/`).
  `terminal/` keeps only the neutral pieces: `umh_forward.*` (the `UmhForwardPolicy`,
  forward/wait over an injected `UmhForwardChannel`, fail-closed),
  `root_script.*` (script text generation), `handoff_probe.*`,
  `umh_command.hpp`, `root_program.hpp`, `rooted_child.hpp` and
  `terminal_input.hpp`.
- `platform/abi.hpp` - the task/cred/symbol/phys owner Schema and View plus the
  mechanical `apply_to()` merge into the frozen transport; `platform/runtime.*`
  - environment probing (iomem cache, seccomp, SELinux enforce);
  `platform/device_facts.*` - the injectable `DeviceProbeOps` fact collector
  (B5-7, fail-closed; kernel-symbol absence is recorded, not fatal).
  The vendor `platform/vivo/` code (vr.ko guard / per-task tag) was deleted in
  vr_guard (a).
- `plugin/` - the countermeasure plugin facility: `controller.hpp` / `policy.hpp`
  (the neutral mechanism (`PluginStage`, `PluginPolicyFor`) and the
  injected controller), the out-of-tree hook `registry.hpp`, and the fail-closed
  `loader.hpp` (digest via the shared `support/sha256.*`). Behaviors
  consume the non-owning
  `contract::Capabilities` aggregate, and the registry, gate and context are
  supplied by the caller. It loads against the C ABI in
  `contract/abi/glk_contract_abi.h` (C++ mapping: `contract/countermeasure.hpp`).
- `memory/` - address resolution and the neutral `LaunchGeometry` bootstrap POD
  (decoupled from the backend profile), heap/page state and route-neutral payload
  encoding.
- `session/` - the shared state container (`core_session.*`; the 43499-specific
  slot in `backend/cve_2026_43499_state.hpp`), runtime configuration
  (`config::*` in `runtime_config.h` / `runtime_paths.h`), stage types,
  handoff probes and the victim pipe context.
- `race/` - the PI race owner and its waiter/owner/consumer threads.
- `support/` - generic result/RAII helpers, time, CLI parsing and the
  fatal-error type.
- `tests/` - host-side fixed-vector and lifecycle tests, including the C/C++
  link probe and the R1 include firewall; test-only probe code is not linked
  into the production binary.

## Wire and profile

- `profile/glkv3.{hpp,cpp}` + `glkv3_parse.{hpp,cpp}` - the production GLKv3
  wire: a bare MessagePack (MPack) document whose root map carries a mandatory
  `schema == 3`, canonical shortest-integer / UTF-8-key-order encoding, no magic
  or standalone header. `decode_neutral` frames it fail-closed before any owner
  is selected.
- `profile/document.hpp` - the neutral framing result (`release`, component
  tokens, ordered sections with key -> Value entries); it never names a field or
  applies a default. `profile/schema.hpp` validates/binds an owner Schema onto a
  typed View in production-strict mode (unknown section/key rejected).
- `profile/glkv3_parse.*` - the only profile reader: it discriminates a GLKv3
  map root (`looks_like_glkv3`), frames it (`frame_v3`) and enforces
  `schema == 3`. `profile/entry.*` owns the stdin/file entry points and the
  component tokens resolve through `contract::*Kind` (the composition root),
  not through a transport-specific vocabulary. Both the v2 object-section
  container (framer + writer) and its component-id header were removed in
  S4 R2c/R2c-2: a v2 document is now rejected outright. v1 JSON never reaches
  native (Kotlin converts it).
- Owner binding lives with the selected backend
  (`backend/cve_2026_43499/backend_profile.*` and `backend/cve_2026_43284`).
- The owner GLKv3 FieldSpec sets are exported to
  `profile-manifest-v3.tsv` (app test resource + profile-core runtime
  resource); the native, Kotlin and extractor tests cross-check their field
  sets against it, and the exporter bare run verifies BOTH copies
  byte-for-byte (header included), so a stale runtime copy cannot pass.
- The combination-token whitelist (`contract::kCombinationCatalog`) is exported
  the same way to `combination-manifest.tsv` (`make -C src combination-manifest`,
  app test resource + profile-core runtime resource, both copies verified
  byte-for-byte). The Kotlin catalogue and the UI dropdown parse that resource;
  no runtime Kotlin hard-codes a token (checked by `CombinationTokenHardcodeTest`).
- The same command writes the test-only `combination-resolve-vectors.tsv` (one
  copy): canonical / cross-backend / case / whitespace / empty / unknown /
  planned / no-catalogue-backend inputs with their native resolution, pinning
  Kotlin `resolve` (exact) + `normalize` (trim, lowercase) against
  `contract::combination_resolve`.
- F1: every catalogue row is also exposed as
  `CombinationId{backend, route, path}` (`combination_id` /
  `combination_from_id`). The path is a third orthogonal axis, not a terminal
  alias (rootchild and shizuku both enter the root child and differ only in the
  step set). The compact `CombinationKind` id and the wire byte are unchanged.

## Compilation boundaries

- Policies, the component catalogue, the contracts, pipeline and orchestrator are
  header-only and host-compilable; the host tests include them directly and the
  R1 firewall (`tests/include_firewall_test.cpp`) rejects a forbidden layer edge
  or a stale whitelist entry.
- Android-only work (real session/race execution, the backend steps and the
  middleware hooks' bodies) stays in the route/backend units, under
  `#if defined(__ANDROID__)` or in files that only the Android build compiles.
- Includes use paths relative to `src/core` (for example,
  `#include "backend/cve_2026_43499/route/tcp_zerocopy_route.h"`) so ownership stays
  visible at call sites without adding every subdirectory to the compiler search
  path.

## Namespaces

Top level: `ghostlock::{contract,pipeline,backend,platform,plugin,terminal,
memory,session,race,kernelsnitch,support,profile,profile_entry,target,
runtime_time}`; `ghostlock::config` is physically under `session/`.
Backend code nests per CVE (`backend::cve_2026_43499::{route,backend_profile}`,
`backend::cve_2026_43284::{ipsec,pagecache,lkm,steps}`), platform code nests as
`platform::{abi,runtime,vivo}`, and the GLKv3 codec is `profile::glkv3`.

## Tests and gates

- `make -C src native-host-tests` - host unit tests (catalogue, contracts,
  policy, pipeline, lifecycle, profile codec, R1 firewall, fake backend,
  manifest, fixed vectors, ...).
- `make -C src ghostlock` - NDK build of `build/native/ghostlock`.
- `make -C src lint-tidy` - clang-tidy subset; must report 0 findings.
- `python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock` -
  attack-function disassembly gate (runs for attack-path changes).
- `./gradlew :app:testDebugUnitTest --offline` - Kotlin agreement tests
  (`ProfileManifestV3AgreementTest`, `RouteCatalogAgreementTest`,
  `CombinationTokenAgreementTest`).
- Session ownership: the per-field owner / borrower / release / termination
  table lives in `session/core_session.hpp`.
