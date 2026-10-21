# 43499 three-route production plan

Status: plan blocked by design-critic; phase 1 Kotlin route-id/port scaffolding implemented. Select native edge + JNI/Kotlin port; AVD success after absolute futex deadline + `val3=*wait` + owner requeue: statusStep=0, successes=1 (gate cve43499-select-success-avd-20261020-1910.md). TCP legacy snapshot route + JNI/Kotlin port; AVD step=40 with zero FakePage; FakePage injectable. `Cve43499RouteRegistry.standard` availability-gates all three ports. TCP still lacks a passing route result.

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件**仍被 design-critic 阻断**；select 已 AVD pass、tcp 未通（状态表 §4）。

## 1. Goal

Wire all three 43499 routes as explicit production paths:
- `multicast_waiter`
- `select_stack`
- `tcp_zerocopy`

Kotlin owns target/value/mode/page/stamp/retry/uid; C++ owns only fixed syscall/actor execution. UI/config stay out of scope.

## 2. Current facts (verified in tree)

- multicast: `NativeAttemptKernelRoute` + `Cve43499PreparedRoute` executes page_fd + actor batch; AVD route gate PASS. It is not a named `MulticastLanding` port.
- select: `landing/select_stack_route.hpp` exists but its own doc says “演员尚未接线” and its `execute()` skeleton only prepares resources/pselect; no JNI, no Kotlin port.
- tcp: only `access_tcp_zerocopy.hpp` exists; `arm_route()` returns false; no landing route, no JNI, no Kotlin port.

Therefore three-route completion is not a naming change: select needs actor wiring, tcp needs a route implementation.

## 3. Kotlin contract

~~~
enum class Cve43499RouteId { MulticastWaiter, SelectStack, TcpZerocopy }

interface Cve43499RoutePort {
    val route: Cve43499RouteId
    fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult
    fun close()
}
~~~

- One JNI per route: `NativeMulticastRoute`, `NativeSelectStackRoute`, `NativeTcpZerocopyRoute`.
- No generic `routeId` parameter at JNI.
- Selection is explicit in the backend plan; missing/unknown route is a named refusal, never fallback.
- `Cve43499PreparedRoute` remains the multicast implementation and gains the explicit route id.

## 4. Native boundary

Each JNI takes one fixed params blob + page + stamp side buffer, returns one fixed result blob. No retry/uid/profile in C++.

### multicast

Keep the existing page_fd + actor batch. Rename/adapter only; no native rewrite in this phase.

### select

Finish `SelectStackRoute<SelectStackRouteLinux>::execute` actor wiring from the retired snapshot, then expose it through `NativeSelectStackRoute`.

Required before code: exact actor order, resource restore on every path, result fields.

### tcp

No route exists. A `TcpZerocopyRoute` must be specified from the legacy snapshot before JNI: zerocopy carrier, actor order, page send, result fields. This is L-level native work, not a thin wrapper.

## 5. Backend selection

~~~
data class Cve43499Plan(
    val route: Cve43499RouteId,
    val stepPlan: KtStepPlan,
    val routeParams: ByteArray,
)
~~~

- backend opens exactly one route port;
- route mismatch -> Failed before any page write;
- select/tcp are never selectable until their JNI gate passes.

## 6. Tests / gates

- Host: route id/port selection, mismatch refusal, no fallback, codec bounds.
- AVD per route: page bytes, actors, close, no panic; select/tcp red until native wiring passes.
- Device gate per route: cold boot, fixed CPUs, one route, dmesg, uid evidence.
- Rollback: each route behind its own construction; absent => named refusal.

## 7. Phases

1. Kotlin route id/port + selection; multicast adapter; host tests.
2. Select native actor wiring + JNI + AVD gate.
3. TCP native route + JNI + AVD gate.
4. Backend plan route selection; all three gates; no UI.

## 8. Non-goals

- No UI/config.
- No generic route JNI.
- No claim that select/tcp already work.
