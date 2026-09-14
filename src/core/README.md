# Native core layout

- `routes/`: route selection, status, route contexts and the shared
  `route_operations.cpp` implementation. The latter replaces the misleading
  historical name `fops.cpp` without changing route timing or translation-unit
  boundaries.
- `memory/`: address resolution, heap/page state and route-neutral payload encoding.
- `session/`: one-invocation state owner and runtime configuration.
- `support/`: generic C++ result and RAII resource helpers plus the link probe.
- `kernelsnitch/`: kernel-address discovery implementation.
- `tests/`: host-side fixed-vector and lifecycle tests, including the C/C++
  link probe; test-only probe code is not linked into the production binary.
- Top-level files: executable orchestration, PI race, profile transport, target ABI and low-level shared helpers. They remain top-level because they connect several categories.

Includes use paths relative to `src/core` (for example,
`#include "routes/tcp_zerocopy_route.h"`) so ownership remains visible at call
sites and does not depend on adding every subdirectory to the compiler search
path.
