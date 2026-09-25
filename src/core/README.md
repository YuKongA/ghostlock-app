# Native core layout

## Components and pipeline

The execution chain is composed from three component kinds, fixed at compile
time by `Pipeline<Frontend, Backend, Middleware>`:

- **frontend** - startup/handoff (`root_child`; `umh_forward` is a declared but
  unavailable placeholder in `frontend_contract.hpp`).
- **backend** - the vulnerability primitive and its write stages
  (`cve_2026_43499`; `cve_2026_64560` is a header-only unavailable placeholder).
- **middleware** - the route realizing one write (`select_stack` / `tcp_zerocopy`
  / `multicast_waiter`).

Where the pieces live:

- `route/component_catalog.hpp` - component ids, availability and the single
  dispatch authority: `combination_supported()`, `DispatchTarget` and
  `dispatch_target_of()`.
- `route/backend_policy.hpp` / `route/backend_contract.hpp` - the declared
  backend identities plus the `BackendIdentity` / `BackendExecution<B, M>`
  concepts and the identity registry (`for_each_backend`).
- `route/frontend_contract.hpp` - the declared frontend ids and reasons.
- `route/route_policy.hpp` - the middleware policies: compile-time capabilities
  plus the static route hooks (`resident_write`, W1/W2 repairs); a policy that
  needs different behavior redeclares the hook and the Android-only definition
  lives in that middleware's route unit. `MiddlewarePolicy` is the contract.
- `route/pipeline.hpp` - `Pipeline<F, B, M>`: the only execution entry. It
  static-asserts the catalogued combination, the middleware and backend
  contracts, exposes the dispatch `target` and returns `RunResult`.
- `route/orchestrator.hpp` - `run_orchestrated_pipeline`: switches on
  `dispatch_target()` and asserts each case against `Pipeline::target`.
- `route/*_route.{h,cpp}` - the per-middleware route classes and entry points
  (`multicast_waiter_route.cpp` / `tcp_zerocopy_route.cpp` /
  `select_stack_route.cpp`), including each policy's Android hook definitions.
- `session/backend/cve_2026_43499_backend.*` - the available backend's W1/W2/W3
  steps, templated on the middleware policy (explicitly instantiated for the
  catalogued middleware); `session/backend/cve_2026_64560_backend.hpp` is the
  unavailable placeholder.

## Other directories

- `attack/`: profile installation and the shared attack helpers.
- `race/`: the PI race owner and its waiter/owner/consumer threads.
- `memory/`: address resolution, heap/page state and route-neutral payload
  encoding.
- `session/`: one-invocation state owner (`exploit_session.*`, which carries the
  per-field ownership contract), runtime configuration, the frontend handoff
  (`root_child_frontend.*`), stage types, handoff probes and the victim pipe
  context.
- `profile/`: the current v3 profile transport (`binary.cpp`; the v2 writer has
  been replaced, v2 documents are still decoded), accessors and entry points.
- `kernel/`: target ABI, constants and offset tables.
- `support/`: generic C++ result/RAII helpers, time and the fatal-error type.
- `legacy/`: v1 (old JSON `offsets.json`) conversion.
- `kernelsnitch/`: kernel-address discovery implementation.
- `tests/`: host-side fixed-vector and lifecycle tests, including the C/C++
  link probe; test-only probe code is not linked into the production binary.

## Compilation boundaries

- Policies, the component catalogue, the contracts and the pipeline are
  header-only and host-compilable; the host tests include them directly.
- Android-only work (real session/race execution, the backend steps and the
  middleware hooks' bodies) stays in the route/backend units, under
  `#if defined(__ANDROID__)` or in files that only the Android build compiles.
- Includes use paths relative to `src/core` (for example,
  `#include "route/tcp_zerocopy_route.h"`) so ownership stays visible at call
  sites without adding every subdirectory to the compiler search path.

## Tests and gates

- `make -C src native-host-tests` - host unit tests (catalogue, contracts,
  policy, pipeline, lifecycle, profile codec, fixed vectors, ...).
- `make -C src ghostlock` - NDK build of `build/native/ghostlock`.
- `make -C src lint-tidy` - clang-tidy subset; must report 0 findings.
- `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock` - attack
  function disassembly gate (runs for attack-path changes).
- Session ownership: the per-field owner / borrower / release / termination
  table lives in `session/exploit_session.hpp`.
