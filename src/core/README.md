# Native core

[中文](README_ZH.md)

GhostLock compiles each supported frontend/backend/middleware combination as a `Pipeline<Frontend, Backend, Middleware>`. The resolved profile selects the combination at runtime.

## Components

| Component | Available | Unavailable placeholder |
| --- | --- | --- |
| Frontend: startup and handoff | `root_child` | `umh_forward` |
| Backend: vulnerability and write stages | `cve_2026_43499` | `cve_2026_64560` |
| Middleware: one write's route | `select_stack`, `tcp_zerocopy`, `multicast_waiter` | |

| File | Responsibility |
| --- | --- |
| [component_catalog.hpp](route/component_catalog.hpp) | IDs, availability, supported combinations, and dispatch targets. The single dispatch authority. |
| [frontend_contract.hpp](route/frontend_contract.hpp) | Frontend identities and execution contract. |
| [backend_policy.hpp](route/backend_policy.hpp), [backend_contract.hpp](route/backend_contract.hpp) | Backend identities, registry, and `BackendExecution<B, M>` contract. |
| [route_policy.hpp](route/route_policy.hpp) | `MiddlewarePolicy`, capabilities, and static hooks such as `w2_fast_repair_*`. Android hook bodies live in each middleware's route unit. |
| [pipeline.hpp](route/pipeline.hpp) | The execution entry. Checks the combination and contracts at compile time and returns `RunResult`. |
| [orchestrator.hpp](route/orchestrator.hpp) | `run_orchestrated_pipeline` dispatches each catalogued target to its checked pipeline. |
| `route/*_route.{h,cpp}` | Route implementations and Android hook bodies. |
| `session/backend/cve_2026_43499_backend.*` | W1/W2/W3 steps templated on middleware, instantiated for each catalogued route. |

## Directories

| Directory | Responsibility |
| --- | --- |
| `attack/` | Profile installation and shared attack helpers. |
| `race/` | PI race owner and waiter, owner, and consumer threads. |
| `memory/` | Address resolution, heap/page state, and payload encoding. |
| `session/` | Invocation state, runtime configuration, frontend handoff, stage types, probes, and victim context. [exploit_session.hpp](session/exploit_session.hpp) records each field's owner, borrowers, and termination point. |
| `profile/` | GLK1 v2 codec, typed model, accessors, and entry points. |
| `kernel/` | Target ABI, constants, and offset tables. |
| `support/` | Result and RAII helpers, time, and fatal errors. |
| `kernelsnitch/` | Kernel-address discovery. |
| `tests/` | Host fixed-vector, lifecycle, contract, and C/C++ link tests. Test probes are excluded from production. |

## Build boundaries and checks

The catalogue, contracts, policies, and pipeline headers compile on the host. Keep Android execution in route/backend units guarded by `__ANDROID__` or compiled only by the Android build. Includes are relative to `src/core`, such as `"route/tcp_zerocopy_route.h"`.

```sh
make -C src native-host-tests
make -C src ghostlock
make -C src lint-tidy
```

The NDK build produces `build/native/ghostlock` and must have no warnings. `lint-tidy` must report zero findings. For attack-path changes, also compare the attack functions and run the [device gates](../../docs/development/engineering-standards.md):

```sh
python3 tools/cmp_disasm.py <baseline> build/native/ghostlock
```
