# Native C++ 当前实现 UML

> 本图描述当前代码，而不是迁移完成后的目标设计。实线表示对象持有或直接数据传递，虚线表示借用、兼容引用或函数调用。三条路线的实现目前仍聚合在 `routes/route_operations.cpp`；各路线 `.cpp` 只负责 context 初始化。

```mermaid
classDiagram
direction LR

class KotlinProfileResolver {
  +merge(index, defaults, device, override)
  +writeActiveProfileJson()
}

class kernel_offsets {
  <<JSON transport POD>>
  +uname_r
  +kernel/layout offsets
  +execution_settings execution
}

class execution_settings {
  <<profile-owned value>>
  +recommended_main_cpu
  +recommended_consumer_cpu
  +heap/race limits
  +w1/w2/w3 attempts
  +tcp/select/multicast timing
  +handoff timing
}

class TargetProfile {
  <<immutable snapshot facade>>
  +kernel_offsets values
  +int loaded
  +supports_multicast_waiter()
  +supports_tcp_zerocopy()
  +supports_select_stack()
  +execution()
  +route_layout()
}

class ExploitSession {
  <<process owner>>
  +runtime_config runtime
  +TargetProfile profile
  +ResolvedAddresses addresses
  +HeapContext heap
  +PiRaceContext race
}

class RuntimeConfig {
  <<session value; legacy C layout>>
  +paths
  +selected CPUs
  +route flags
}

class ResolvedAddresses {
  <<session value>>
  +kernel image/data aliases
  +physical load
  +init_cred
}

class HeapContext {
  <<session-owned compatibility context>>
  +KernelSnitchContext snitch
  +MmContextSet prepare/spray/pre/post
  +unique_ptr skb_buffer
  +UniqueFd leak_memfd + pid leak_child
  +current/prebuilt/quarantine PayloadPage
}

class VictimContext {
  <<run-local owning class (CPP12)>>
  +UniqueFd task/cmd/uid pipe ends
  +child pid stays with run_exploit
}

class HandoffProbe {
  <<session helper; KernelSU handoff (CPP12)>>
  +HandoffPollPolicy policy
  +HandoffProbeResult result
  +module / ksu log / enforce probes
}

class WriteRequest {
  <<immutable borrowed request>>
  +target
  +mode
  +preserve_child
}

class PiRaceContext {
  <<session-owned race class (CPP09)>>
  +futex words
  +atomics (seq_cst hot path, relaxed reset)
  +PthreadOwner waiter/owner/consumer
  +reset / start_threads / run
  +request_stop / join
  +RouteStatus route_status
  +outcome_with_counters()
}

class RouteController {
  <<stack-local dispatcher>>
  +PiRaceContext* race
  +TargetProfile* profile
  +RouteKind selected
  +execute(request) RouteStatus
}

class MulticastWaiterRouteContext {
  <<route-local / resident singleton>>
  +MulticastWaiterLayout layout
  +workers and futexes
  +socket_fd
  +RouteStatus status
}

class TcpZerocopyRouteContext {
  <<route-local owning class (CPP10)>>
  +UniqueFd client/server/punch
  +MappedRegion mapping
  +PthreadOwner punch_worker
  +prepare / execute / disarm / destroy
  +RouteStatus status
}

class SelectStackRouteContext {
  <<route-local owning class (CPP11)>>
  +FdSet in/out/ex and owned sets
  +UniqueFd pipe/timerfd/high-read
  +BorrowedFd stdio_backup
  +prepare / execute / disarm / destroy
  +RouteStatus status
}

class RouteOutcome {
  <<layout-compatible typed status>>
  +RouteCode code
  +int step
  +int error_number
  +is_clean() bool
  +is_dirty() bool
  +can_fallback() bool
}

class RouteOperations {
  <<routes/route_operations.cpp>>
  +multicast prepare/execute/disarm/destroy
  +tcp prepare/execute/disarm/destroy
  +select prepare/execute/disarm/destroy
}

class NativeResource {
  <<support; PthreadOwner wired into PiRace (CPP09)>>
  +BorrowedFd
  +UniqueFd
  +MappedRegion
  +ScopeExit
  +PthreadOwner
  +ChildProcess
  +Result~T,SysError~
}

KotlinProfileResolver --> kernel_offsets : active-profile.json
kernel_offsets *-- execution_settings
kernel_offsets --> TargetProfile : load + snapshot
ExploitSession *-- TargetProfile
ExploitSession *-- RuntimeConfig
ExploitSession *-- ResolvedAddresses
ExploitSession *-- HeapContext
ExploitSession *-- PiRaceContext
TargetProfile --> ResolvedAddresses : offsets
TargetProfile --> HeapContext : geometry + limits
TargetProfile --> RouteController : capabilities
TargetProfile --> MulticastWaiterRouteContext : layout + timing
TargetProfile --> TcpZerocopyRouteContext : layout + timing
TargetProfile --> SelectStackRouteContext : layout + timing
HeapContext ..> WriteRequest : builds payload page for
PiRaceContext ..> WriteRequest : borrows
RouteController o-- PiRaceContext : borrows
RouteOperations ..> VictimContext : W2/W3 protocol
ExploitSession ..> HandoffProbe : run_exploit() (session helper)
RouteController ..> MulticastWaiterRouteContext : dispatch
RouteController ..> TcpZerocopyRouteContext : dispatch
RouteController ..> SelectStackRouteContext : dispatch
RouteController --> RouteOutcome : returns
RouteOperations ..> MulticastWaiterRouteContext : operates on
RouteOperations ..> TcpZerocopyRouteContext : operates on
RouteOperations ..> SelectStackRouteContext : operates on
NativeResource ..> ExploitSession : foundation only
```

## 当前关键边界

- `kernel_offsets` 是 Kotlin JSON 到 Native 的可变 transport；`TargetProfile` 是复制得到的只读语义入口，但两者尚未完全拆成独立 C++ 类型。
- `ExploitSession` 已集中主要全局状态；`RuntimeConfig`/`ResolvedAddresses` 是值类型（CPP04/CPP08），`PayloadPage` move-only（CPP07），CPU 镜像已删除且 config 经 `runtime_config_snapshot()` 访问（CPP12/`SESSION-01`），路径宏也已内联（`e1782f6`，与门禁版逐字节一致）；Heap 侧 `MmContextSet`（owning vector，close/kill 显式）、`UniqueFd leak_memfd`、`unique_ptr skb_buffer` 已收编且 `page_base`/`fake_*` 别名已删除（CPP12/`CPP07-OWNER`，门禁 `CPP12h`），`CORE`/`mm_struct_sz()` 宏已由显式 CPU 与 `target_profile_mm_struct_sz()` 取代（门禁 `CPP12j`）。`VictimContext`（header-only，pipe 集合）已完成两次冷机门禁（`CPP12m`/`CPP12n`）；`handoff_probe_run` 收拢 KernelSU 探针（门禁 `CPP12i`）。其余引用 façade（profile/address/race）仍待收编。
- 三条路线共享 `WriteRequest → HeapContext → PiRaceContext → RouteController`，随后才各自构造 route context。
- `PiRaceContext` 是 `ghostlock::PiRace`：futex、原子量、三个 `PthreadOwner` 与 `RouteStatus` 由该类唯一拥有，`start_threads`/`run`/`request_stop`/`join` 显式分离，`run()` 返回合并 consumer calls/success 的结果；`g_pi_race_context` 仍是 session 成员的引用别名（CPP12 删除）。
- RAII 基础类型已补齐借用、scope、stop 和 handoff 语义；`PthreadOwner` 已接入 `PiRace`（CPP09）与 `TcpZerocopyRoute`（CPP10），`SelectStackRoute`（CPP11）拥有 `FdSet`/`UniqueFd` 并把 stdio 备份保持为 `BorrowedFd`；Multicast 仍必须显式执行 `disarm → destroy`，以防内核继续引用 fd、mmap 或线程相关对象。
- 下一次结构拆分是把 `route_operations.cpp` 的三组函数分别移入对应路线 `.cpp`；该操作会改变编译单元和生成代码，必须独立进行真机门禁。
