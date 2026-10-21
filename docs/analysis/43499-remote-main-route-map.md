# remote/main route reference map

Time: 2026-10-20. Branch: dev-nocpp. Reference: origin/main (847793ea).

## Remote implementations

- `src/core/route/multicast_waiter_route.cpp` 159 lines
- `src/core/route/select_stack_route.cpp` 525 lines
- `src/core/route/tcp_zerocopy_route.cpp` 365 lines
- `src/core/tests/select_stack_route_test.cpp`
- `src/core/tests/tcp_zerocopy_route_test.cpp`

These establish that the three routes exist and have tests on remote/main; they do not compile into dev-nocpp unchanged.

## API deltas

| remote/main | dev-nocpp |
| --- | --- |
| `src/core/route/*` | `core/src/backend/cve_2026_43499/landing/*` |
| `memory::WriteRequest` | `memory::WriteRequest` but different headers |
| `profile::TargetProfile` / `SelectStackLayout` | profile moved out; route config is now explicit structs |
| `race::PiRace` with `consumer_go/calls/success` atoms | `race::PiRace<Seam>` only owns chain/target; current select route has its own actors |
| `support::UniqueFd/BorrowedFd` | current route uses raw fd fields + cleanup |
| `route/RouteStatus` | `landing::SelectStackReport` |

## Current dev-nocpp status

- multicast: `NativeAttemptKernelRoute` + `Cve43499PreparedRoute`; AVD gate PASS.
- select: `SelectStackRouteLinux` + explicit instantiation compile. `native_select_stack_jni.cpp` + Kotlin codec/port implemented. AVD NativeSelectStackInstrumentedTest PASS (JNI result returned; route success not asserted).
- tcp: `access_tcp_zerocopy.hpp` only; `arm_route()` false. Added `route/tcp_zerocopy_route_legacy.cpp` copied from the reference snapshot, `native_tcp_route_jni.cpp`, Kotlin codec and `Cve43499TcpZerocopyPort`; it compiles, but still uses the snapshot's hardcoded offsets/engine and has no AVD gate.

## Port order

1. select: bind current `SelectStackRoute<SelectStackRouteLinux>` to a fixed JNI params/result blob; Kotlin port supplies target/value/config/selected fds.
2. tcp: adapt remote `tcp_zerocopy_route.cpp` to current `memory`/`race`; JNI + Kotlin port.
3. multicast: name/adapter only; no native rewrite.
4. unified registry.

## Non-goals

- No direct copy of remote files without API adaptation.
- No claim that remote/main passes are current-branch evidence.
- No UI/config.
