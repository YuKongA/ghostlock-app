English | [简体中文](adding-a-component_ZH.md)

# How to add a component (middleware / backend / frontend)

GhostLock fixes the execution chain at compile time through `Pipeline<Frontend, Backend, Middleware>`:

- **frontend**: startup and handoff. Currently `root_child`; `umh_forward` is a declared but unavailable placeholder.
- **backend**: exploit primitives and write steps. Currently `cve_2026_43499`; `cve_2026_64560` is an unavailable header-only placeholder.
- **middleware**: the route for one write: `select_stack` / `tcp_zerocopy` / `multicast_waiter`.

The following are the sole authorities for component selection, composition, and execution:

- Availability: `frontend_available` / `backend_available` / `middleware_available`. Neither identities nor execution policies carry `available`.
- Composition and dispatch: `combination_supported(selection)` lists dispatchable triples. `dispatch_target_of(frontend,
  backend, middleware)` gives the dispatch target for a complete combination. Each case in `orchestrator.hpp` locks this down with
  `static_assert(P::target == …)`. The dispatch key retains the full selection. The current catalog contains only
  `root_child × cve_2026_43499 × {the three middleware kinds}`. Adding a frontend or backend must also extend the
  `DispatchTarget` enum, mapping, and orchestrator branches. Middleware alone must not determine the pipeline.
- Execution entry point: `Pipeline<F,B,M>::run` in `route/pipeline.hpp` checks `catalogued`,
  `MiddlewarePolicy<M>`, `FrontendExecution<F>`, and `BackendExecution<B,M>` at compile time, and returns `RunResult`.
- Binding: backend steps are templated on the middleware through `Cve2026_43499Policy::run<M>` / `attack_write<M>`.
  They call static hooks such as `M::w2_fast_repair_prebuild` directly, without a vtable. Hook boundaries use `[[gnu::noinline]]`.

Adding a component is an L-level change. First read `design-philosophy.md` and `engineering-standards.md`, produce a plan, and obtain approval.
Then follow this guide.

---

## 1. Add middleware (a route)

### Change overview

| Area | File | Change |
|---|---|---|
| Native registration | `profile/model.h` | A `RouteKind` value, `kRouteFooWaiter`, and a row in `kRouteCatalog` |
| Native availability | `route/component_catalog.hpp` | Add the new kind to `middleware_available` |
| Native policy | `route/route_policy.hpp` | Add `FooPolicy` with capabilities, `supported`/`run`, and required hook override declarations; append it to `RoutePolicyList` |
| Native dispatch | `route/component_catalog.hpp` + `route/orchestrator.hpp` | Add a `DispatchTarget` value, a `dispatch_target_of` mapping, and an orchestrator case with `Pipeline<…, FooPolicy>` and a target `static_assert` |
| Native instantiation | `session/backend/cve_2026_43499_backend.cpp` | Explicitly instantiate `run<FooPolicy>` / `attack_write<FooPolicy>` |
| Native implementation | `route/foo_route.{h,cpp}` | Route class, `do_foo_fake_lock_route`, and Android definitions of policy hooks |
| Native build | `src/Makefile`, `src/CMakeLists.txt` | Add `core/route/foo_route.cpp` |
| Native transport | `profile/binary.cpp` | Route field table, if there are private parameters |
| Native export | `profile-core/.../profile/ProfileExporter.kt` | No change needed: export iterates over all routes through `RouteKind.entries` |
| Kotlin registration | `data/route/RouteKind.kt` | `(token, wire)` and configuration construction |
| Kotlin parameters | `data/route/FooConfig.kt` | Add when there are private parameters |
| Tests | See section 4 | catalog/policy/catalog-dispatch/contract/route/cmp_disasm |

Use this table as the checklist.

### 1. Register the route

Register the same token and wire value on both sides. `src/core/tests/route_catalog_test.cpp` and
`app/src/test/.../RouteCatalogAgreementTest.kt` assert the same list. Missing either side makes the tests fail.

### 2. Add private parameters, if needed

Put parameters used only by this route in its route extension section. The field names and types in Kotlin `FooConfig`'s `entries()/apply()/from()`
must match the field table in native `profile/binary.cpp`. Put parameters read by shared code in a common section.
Named sections and entries are unordered. `ProfileExporter` and `ProfileResolver.nativeValue` retrieve values uniformly through `RouteKind`.
A new route does not need its path registered on the export side.

### 3. Implement the route

In `route/foo_route.{h,cpp}`, the Route class must satisfy `RouteLifecycle`: `prepare → execute → disarm → destroy`.
Report results only through `status`. Do not use a virtual base class. Keep the upper portion host-compilable.
Place entry-point and hook definitions in a `#if defined(__ANDROID__)` block.

### 4. Add the policy and hooks

Add `FooPolicy : RoutePolicyDefaults` to `route/route_policy.hpp`:

- **Capabilities**, declared as compile-time `static constexpr bool` values: `multicast`, `w2_fast_repair`, `w3_exact_target`,
  `tcp_payload_layout`, and `allows_fallback`. Declare only those that need to be `true`. Inherit the defaults for the rest.
- **Hooks with side effects**: `w2_fast_repair_prebuild/activate(session)`. `RoutePolicyDefaults` provides defaults.
  When overriding a hook, define it in that unit and mark its declaration `[[gnu::noinline]]` to prevent LTO from inlining route implementation into attack functions.
- **Pure queries**, such as the W1 retry limit for a one-shot route, whether scratch repair is needed, or whether W3 requires an exact hit:
  do not add a hook. Express them in backend steps with `if constexpr (M::…)` and profile runtime values.

### 5. Add dispatch and instantiation

Add the `DispatchTarget` value and the mapping for the kind to the catalog. Add an orchestrator case:

```cpp
case DispatchTarget::RootChild_Cve43499_FooWaiter: {
    using P = Pipeline<session::frontend::RootChildPolicy,
                       session::backend::Cve2026_43499Policy,
                       route::FooPolicy>;
    static_assert(P::target == DispatchTarget::RootChild_Cve43499_FooWaiter,
                  "dispatch case must match the pipeline's target");
    return P::run(exploit_session, decoded, debug_dir, force_attack);
}
```

Add explicit instantiations for the policy at the end of `cve_2026_43499_backend.cpp`. Omitting them causes a link failure.
`DispatchTarget` names include the full combination prefix so that mappings and branches grow together when the backend or frontend dimension expands.

---

## 2. Add a backend

| Area | File | Change |
|---|---|---|
| Identity | `route/backend_policy.hpp` | `FooBackend` and a `static_assert` binding it to the catalog |
| Availability/combinations | `route/component_catalog.hpp` | `BackendKind` value, `backend_available`, `combination_supported`, and dispatch dimension |
| Contract registration | `route/backend_contract.hpp` | Add `FooBackend` to `BackendIdentityList` |
| Execution policy | `session/backend/foo_backend.{hpp,cpp}` | `FooPolicy` with `kind` and `run<M>` / `attack_write`, identity-binding assertions, and explicit instantiations |
| Dispatch | `route/orchestrator.hpp` | `Pipeline` branches for the new backend × middleware combinations |
| Build/tests | `src/Makefile` + `backend_contract_test` + `profile_binary_test` + Kotlin | |

**Unavailable backend placeholders** provide only an identity, `kind`, and `unavailable_reason`. See
`session/backend/cve_2026_64560_backend.hpp`. Do not implement `run` or instantiate `Pipeline`. The catalog
expresses availability. The wire carries only the id. Backends have no private section at present. Future fields must go in their own section and must not reuse 43499's slots.

---

## 3. Add a frontend

Follow the same structure as a backend. `route/frontend_contract.hpp` provides `FrontendIdentity`, containing `kind` and optionally
`unavailable_reason`, and `FrontendExecution<F>`, which requires an available frontend's `run(session, chain)` to return exactly
`StageResult`. It also provides the `FrontendIdentityList` / `for_each_frontend` registry. The execution policy is in
`session/root_child_frontend.hpp`. `RootChildPolicy::run` handles startup and handoff. That header binds
the identity and catalog with `static_assert`. `Pipeline` constrains the composition entry point with
`static_assert(FrontendExecution<F>)`. See `frontend_contract.hpp` for UMH's responsibility boundaries:
keep the child lifecycle separate from KernelSU handoff. UMH must not bind to KernelSU by default.

---

## 4. Build, test, and pass the gates

Adding a component requires at least these tests:

1. `route_catalog_test.cpp` / `RouteCatalogAgreementTest.kt`: synchronize the canonical list for middleware.
2. `route_policy_test.cpp`: capability assertions and dispatch counts for middleware.
3. `component_catalog_test.cpp`: combinations, `dispatch_target`, and mappings for new combinations.
4. `backend_contract_test.cpp`: `FrontendIdentity` / `FrontendExecution` / `BackendIdentity` /
   `BackendExecution` / `Pipeline::target` for a new frontend, backend, or middleware.
5. `foo_route_test.cpp`: construction, destruction, and geometry for middleware. Register it in `NATIVE_HOST_TESTS`.
6. `profile_binary_test.cpp`: wire decoding and rejection for a new id.
7. When adding an attack-path function, add it to `TARGETS` in `tools/cmp_disasm.py`.

```sh
make -C src ghostlock            # Device binary
make -C src native-host-tests    # Host unit tests
make -C src lint-tidy            # clang-tidy (0 findings)
python3 tools/cmp_disasm.py <baseline> build/native/ghostlock
./gradlew :app:testDebugUnitTest --offline
```

Finally, run once on a device: cold device, fixed CPU pair, and KernelSU not loaded. Confirm that the component
is selected and write verification passes. Archive the result in the `docs/analysis/device-gates/` format.
Do not mark it supported before it passes on a device. See the verification and evidence standards in
`engineering-standards.md` for the complete gates.
