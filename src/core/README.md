# Native core layout

## Components and pipeline

The execution chain is `Pipeline<Backend, Terminal>`, fixed at compile time:

- **terminal** - startup/handoff. `root_child` is available; `umh_forward` landed
  its execution policy in B5-8 but is not device-verified, so it stays unavailable
  in `contract/identity.hpp`.
- **backend** - the vulnerability primitive and its write stages.
  `cve_2026_43499` is available; `cve_2026_43284` is wired but not
  device-verified; `cve_2026_64560` / `cve_2026_31431` / `cve_2026_43503` /
  `cve_2026_23274` are header-only unavailable placeholders.
- **route** - the backend-internal strategy realizing one write
  (`select_stack` / `tcp_zerocopy` / `multicast_waiter`). It is chosen by the
  backend from the resolved profile and is **not** an assembly axis.

The catalogue wires three sparse triples: `cve_2026_43499 x {w1_w3, w1_w2} x
root_child` (available) and `cve_2026_43284 x pagecache_write x umh_forward`
(catalogued/wired, availability false until the B5-9 device gate).

Where the pieces live:

- `contract/identity.hpp` - the stable component ids, per-axis availability
  (`backend_available` / `terminal_available` / `stepset_available`, with
  `selection_supported()` as the runtime fail-closed gate), the neutral terminal
  interface vocabulary (`TerminalInput` / `RootProgram` / `ActivationContext`)
  and the `BackendIdentity` / `BackendExecution<B, Input>` / `BackendState` /
  `TerminalIdentity` / `TerminalExecution` concepts with the identity registries
  (`for_each_backend`, `for_each_terminal`). It must stay host-compilable and
  must not include `backend/`, `pipeline/`, `platform/` or `terminal/`.
- `pipeline/component_catalog.hpp` - the sparse wired dispatch catalogue
  (`combination_supported()`, `DispatchTarget`, `dispatch_target_of()`,
  `middleware_available()`, and the wire token/name helpers). This is the
  composition authority; the vocabulary comes from `contract/identity.hpp`.
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
  implementations under `route/`, and the owner binding / GLKv3 schema in
  `backend_profile/` + `glkv3_schema.hpp` + `schema.hpp`.
- `backend/cve_2026_43284/` - the wired-but-unavailable backend: `ipsec/`,
  `pagecache/`, `lkm/`, `steps/`, `backend_terminal.hpp`, `session_frame.*`,
  `glkv3_schema.hpp`, `schema.hpp`; `backend/cve_2026_43284_backend.hpp` holds
  `Cve2026_43284Policy`. The placeholder backends have one header each
  (`backend/cve_2026_64560_backend.hpp` etc.).
- `backend/victim/` - the victim child lifecycle and pipe context.
- `terminal/root_child.*` - the available terminal (`RootChildPolicy`,
  `run_root_child_handoff`); `terminal/umh_forward.*` - the `UmhForwardPolicy`
  (forward/wait over an injected `UmhForwardChannel`, fail-closed). The neutral
  terminal input payloads are in `terminal/terminal_input.hpp` /
  `terminal/rooted_child.hpp` / `terminal/root_program.hpp`.
- `platform/abi.hpp` - the task/cred/symbol/phys owner Schema and View plus the
  mechanical `apply_to()` merge into the frozen transport; `platform/runtime.*`
  - environment probing (iomem cache, seccomp, SELinux enforce);
  `platform/device_facts.*` - the injectable `DeviceProbeOps` fact collector
  (B5-7, fail-closed; kernel-symbol absence is recorded, not fatal);
  `platform/vivo/` - the vendor vr.ko guard.
- `ancillary/` - the neutral ancillary mechanism (`AncillaryStage`,
  `AncillaryOps`, `AncillaryPolicyFor`) and the injected controller; the
  registry, gate and context are supplied by the caller.
- `memory/` - address resolution and the neutral `LaunchGeometry` bootstrap POD
  (decoupled from the backend profile), heap/page state and route-neutral payload
  encoding.
- `session/` - the shared state container (`core_session.*`; the 43499-specific
  slot in `backend/cve_2026_43499_state.hpp`), runtime configuration
  (`config::*` in `runtime_config.h` / `runtime_paths.h`), stage types,
  handoff probes and the victim pipe context.
- `race/` - the PI race owner and its waiter/owner/consumer threads.
- `kernelsnitch/` - the frozen upstream kernel-address discovery implementation.
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
- `profile/binary.*` - the legacy v2 container (object sections). Production
  writes GLKv3 and reads v3 + v2; the v2 writer is behind
  `GHOSTLOCK_ENABLE_V2_WRITER` (host tests only). `profile/entry.*` owns the
  stdin/file entry points; v1 JSON never reaches native (Kotlin converts it).
- Owner binding lives with the selected backend
  (`backend/cve_2026_43499/backend_profile.*` and `backend/cve_2026_43284`).
- The owner Schema / GLKv3 FieldSpec sets are exported to
  `app/src/test/resources/profile-manifest.tsv` and
  `profile-manifest-v3.tsv`; the native, Kotlin and extractor tests cross-check
  their field sets against them.

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

Top level: `ghostlock::{contract,pipeline,backend,platform,ancillary,terminal,
memory,session,race,kernelsnitch,support,profile,profile_entry,binary_profile,
target,runtime_time}`; `ghostlock::config` is physically under `session/`.
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
  (`ProfileManifestAgreementTest`, `ProfileManifestV3AgreementTest`,
  `RouteCatalogAgreementTest`).
- Session ownership: the per-field owner / borrower / release / termination
  table lives in `session/core_session.hpp`.
