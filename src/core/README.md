# Native core layout

- `route/`: route selection, status, per-route contexts and their route-level
  drivers (`multicast_waiter_route.cpp` / `tcp_zerocopy_route.cpp` /
  `select_stack_route.cpp`), the pluggable `route_policy.hpp` registry and the
  shared `ExploitProcedure` base.
- `attack/`: profile installation and the shared attack helpers.
- `race/`: the PI race owner and its waiter/owner/consumer threads.
- `memory/`: address resolution, heap/page state and route-neutral payload encoding.
- `session/`: one-invocation state owner, runtime configuration, handoff
  probes and the victim pipe context.
- `profile/`: the v2 profile transport (`binary.cpp`), accessors and entry points.
- `kernel/`: target ABI, constants and offset tables.
- `support/`: generic C++ result/RAII helpers, time and the fatal-error type.
- `legacy/`: v1 (old JSON `offsets.json`) conversion.
- `kernelsnitch/`: kernel-address discovery implementation.
- `tests/`: host-side fixed-vector and lifecycle tests, including the C/C++
  link probe; test-only probe code is not linked into the production binary.

Includes use paths relative to `src/core` (for example,
`#include "route/tcp_zerocopy_route.h"`) so ownership stays visible at call
sites without adding every subdirectory to the compiler search path.
