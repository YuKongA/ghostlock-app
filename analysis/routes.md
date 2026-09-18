# 三条路线、数据流与清理

## 核心维护图：三路线端到端主链

此图是迁移阶段的唯一强制更新 UML。实线表示当前实现，设备验证状态直接写在路线节点中。当前全部核心翻译单元已用 C++20/静态 libc++ 构建；Multicast 门禁已连续通过（`CPP04-06`、`CPP06b`、`CPP07`、`CPP08-direct`/`-shizuku`、`CPP09`、`CPP12`、`CPP12d`、`CPP12f`、`CPP12g`、`CPP12h`、`CPP12i`、`CPP12j`，均在 `device-gates/`）；PI-TIMEOUT-01 因构建布局下 3/3 panic 已回退（`CPP12b/c/e`，见规划 TODO 表）。TCP/Select 仍只有主机固定测试。

```mermaid
flowchart TD
    Assets["assets: index + defaults<br/>per-device profile JSON"] --> Resolve["Kotlin ProfileConfiguration<br/>match + merge + validate"]
    Override["optional user override"] --> Resolve
    Resolve --> Active["resolved active-profile.json"]
    Active --> Launch["Kotlin ProcessBuilder<br/>--profile + environment + CPU selection"]
    Launch --> Binary["Native binary<br/>all core translation units C++20 / static libc++"]
    Binary --> Main["main.cpp / run_exploit()"]
    Main --> Session["ExploitSession process owner<br/>config + profile + addresses + heap + PI"]
    Session --> Config["runtime_config_init()"]
    Config --> Snapshot["RuntimeConfig snapshot<br/>CPU + paths + route flags"]
    Session --> Decode["load_resolved_profile_json()<br/>UniqueFd + std::string owned input"]
    Decode --> Validate["release + range + route validation"]
    Validate --> Profile["immutable TargetProfile snapshot<br/>owns resolved values"]
    Profile --> Capability["semantic capabilities + layouts<br/>M: device verified; T/P: static only"]
    Profile --> Execution["execution settings<br/>CPU + heap + race + stages + routes + handoff"]
    Execution --> Snapshot
    Profile --> Addresses["ResolvedAddresses<br/>SoC + physical load + init_cred"]
    Addresses --> Init["direct address translation<br/>init_p0_profile() diagnostics"]
    Init --> W1["W1: retry_write_stage()"]
    W1 --> Request["immutable WriteRequest<br/>target + mode + preserve-child"]
    W2 -. next request .-> Request
    W3 -. next request .-> Request
    Request --> Write["do_one_write(request)"]
    Write --> Page["prepare_good_kernel_page()"]
    Page --> Snitch["KernelSnitchContext<br/>init → find → scan → result → destroy<br/>owns FutexHashContext"]
    Snitch --> Payload["PayloadWriteLayout + bounded std::span encoder"]
    Request --> Payload
    Capability --> Payload
    Execution --> Page
    Execution --> Race
    Payload --> Encoders["Multicast / TCP / Select<br/>waiter encoders"]
    Encoders --> Race
    Write --> Race["run_main_route_threads()<br/>PiRace (PthreadOwner x3)"]
    Snapshot --> Race
    Profile --> Race
    Addresses --> Page
    Race -. pthread .-> Waiter["waiter_thread()"]
    Race -. pthread .-> Owner["owner_thread()"]
    Race -. pthread .-> Consumer["consumer_thread()"]
    Waiter --> Controller["RouteController<br/>supports + execute + RouteStatus"]
    Controller --> Choice{"route"}
    Snapshot --> Choice
    Choice --> M["Multicast one-shot<br/>CPP03 device verified"]
    Choice --> T["TcpZerocopyRouteContext<br/>static verified"]
    Choice --> P["SelectStackRouteContext<br/>static verified"]
    T -. "clean + disarmed only" .-> P
    M --> Status["RouteStatus<br/>OK / fallback-safe / dirty"]
    T --> Status
    P --> Status
    Status --> Verify["stage verification"]
    Verify --> W1
    W1 --> W2["W2: spawn_victim() + credential stage"]
    W2 --> W3["W3: flags/seccomp mode when needed"]
    W3 --> Root["child_main() / KernelSU handoff"]
    Root --> Cleanup["route cleanup / resident stop"]
    Cleanup --> Exit["native exit + Kotlin log"]
```

S02–S14 已完成 context 与统一路线状态边界。CPP00 只改变编译/链接层：C 攻击代码保持原控制流，额外的 C++20 link probe 不被攻击链调用；静态 libc++ 产物已完整通过 W1/W2/W3 与 KernelSU 门禁。one-shot Multicast 仍保留已验证的专用小栈帧实现；TCP/Select 仍等待对应设备。三条路线继续共用 profile、地址转换、堆页准备、PI 三线程和阶段验证，只在“用哪种可控结构覆盖 stale waiter”上分叉。

## PI竞争时序

```mermaid
sequenceDiagram
    participant M as run_main_route_threads
    participant W as waiter_thread
    participant O as owner_thread
    participant C as consumer_thread
    participant K as kernel futex/rt_mutex

    M->>W: pthread_create
    M->>O: pthread_create
    M->>C: pthread_create
    W->>K: LOCK_PI(f_pi_chain)
    O->>K: LOCK_PI(f_pi_target)
    W->>K: WAIT_REQUEUE_PI(f_wait -> f_pi_target)
    M->>K: CMP_REQUEUE_PI
    Note over W,K: waiter进入易受影响的remove_waiter生命周期
    W->>W: 调用选中的route
    W->>C: punch_consume_go
    C->>K: sched_setattr_tid(waiter_tid)
    Note over C,K: PI调整遍历被覆盖的waiter/task/lock
    W-->>M: route_done
    M->>W: join
    M->>O: stop + join
    M->>C: stop + join
```

## 5.x Multicast

```mermaid
flowchart TD
    W["waiter_thread()"] --> Route["do_kernel5_fake_lock_route()"]
    Route --> Sock["UDP socket"]
    Route --> Stamp["setsockopt(MCAST_BLOCK_SOURCE)"]
    Stamp --> Overlap["option buffer overlaps stale compact waiter"]
    Route --> Go["punch_consume_go"]
    Go --> C["consumer_thread()"]
    C --> Adjust["sched_setattr_tid()"]
    Adjust --> PI["PI walk"]
    PI --> Fake["fake_task / fake_lock"]

    Start["kernel5_resident_start()"] -. pthread .-> X["mr_x()"]
    Start -. pthread .-> Y["mr_y()"]
    Y --> MS["mr_stamp()"]
    RW["kernel5_resident_write()"] --> MA["mr_adjust()"]
    RW --> Y
    Stop["kernel5_resident_stop()"] --> Join["join + socket/page cleanup"]
```

- 非resident路径的临时socket由route关闭，但waiter还会执行额外futex slow path来disarm `pi_blocked_on`。
- resident路径的线程、socket、reclaim和page状态由 `kernel5_resident_stop()` 收尾。
- W1的scratch quarantine/repair/release位于 `run_exploit()`，因此它与路线内清理并未真正分开。

## 6.1 TCP Zerocopy

```mermaid
sequenceDiagram
    participant R as do_tcp_fake_lock_route
    participant P as tcp_punch_thread
    participant S as loopback TCP
    participant C as consumer_thread
    participant K as kernel

    R->>R: tcp_make_pair + memfd + mmap
    R->>P: pthread_create
    P->>P: fallocate fill / punch hole
    R->>S: send
    R->>K: getsockopt(TCP_ZEROCOPY_RECEIVE)
    Note over R,K: zerocopy参数区与stale waiter布局重叠
    R->>C: punch_consume_go
    C->>K: sched_setattr_tid
    R->>P: stop + join
    R->>R: munmap + close memfd/socket
```

TCP是局部资源所有权最集中的路线。CPP10 后由 `TcpZerocopyRoute` 唯一拥有 client/server/punch fd、mapping 与 punch worker（`UniqueFd`/`MappedRegion`/`PthreadOwner`），`prepare → execute → disarm → destroy` 显式分离；join/munmap 失败置 dirty 并保留资源（不提前 close 已被 puncher 借用的 fd）。PI三线程和喷射页仍由外层公共代码管理。

## pselect/select

```mermaid
flowchart TD
    R["do_pselect_fake_lock_route()"] --> Ctor["SelectStackRoute(race, request, layout, stdio_backup)"]
    Ctor --> Prep["prepare(): pipe/timerfd + select_stack_build_fdsets()"]
    Prep --> Shift["pselect_waiter_shift()"]
    Prep --> Word["pselect_put_waiter_word()"]
    Word --> Sets["FdSet in/out/ex"]
    Prep --> Open["open_selected_fds()"]
    R --> Kind{"compact_waiter?"}
    Kind -->|6.1 fallback| PS["pselect()"]
    Kind -->|6.6/6.12| S["select()"]
    PS --> Frame["stack waiter layout"]
    S --> Frame
    R --> C["consumer_thread()"]
    C --> PI["PI walk / tree relink"]
    R --> Restore["destroy(): restore stdio (borrowed)"]
    R --> Check{"consumer_inflight?"}
    Check -->|no| Close["close owned timerfd/pipe"]
    Check -->|yes| Dirty["retain every route fd until process exit; step=34"]
```

consumer仍停在内核时不关闭相关fd，是为避免回收它仍在引用的对象；CPP11 后由 `SelectStackRoute` 经 `release_to_process_lifetime` 显式保留（stdout/stderr 备份是 `BorrowedFd`，从不关闭）。这个出口是dirty failure，不适合运行时切换路线。

## 堆与地址数据流

```mermaid
flowchart LR
    P["TargetProfile read-only view"] --> A["resolve_address_space()"]
    A --> O["ResolvedAddresses<br/>authoritative snapshot"]
    O --> C["compatibility mirrors<br/>active_offsets / legacy addresses"]
    O --> D["init_p0_profile() / data_addr()"]
    D --> Targets["stage target addresses"]
    P --> KS["setup_kernelsnitch()"]
    KS --> Coll["futex hash collisions"]
    Coll --> MM["mm_struct candidate"]
    MM --> Page["prepare_kernel_page()"]
    Page --> Ctx["prepare/spray/pre/post<br/>ghostlock::MmContextSet"]
    Page --> SKB["prepare_skb_payload()"]
    Targets --> SKB
    SKB --> Fake["g_heap_context.current<br/>fake_lock/waiter/task/fops"]
    Fake --> Routes["three routes"]
    Routes --> Verify["W1/W2/W3 verify callback"]
```

## 路线选择与回退现状

S15 最终回归（`versionCode=176`）再次验证 Multicast 端到端主链：六次 route 均为 `OK clean=1/1`，W1/W2/W3 后进入 `KernelSU ready`。CPP07–CPP09 的门禁在同一行为基线上复验通过（`CPP09-20260917-multicast-pass`）。TCP、Select 和 TCP→Select 回退仍等待对应设备补证。

```mermaid
stateDiagram-v2
    [*] --> ProfileSelected
    ProfileSelected --> Multicast: kernel_major == 5 and mcast profile
    ProfileSelected --> TCP: compact_waiter and TCP env enabled
    ProfileSelected --> PselectCompact: compact_waiter and GHOSTLOCK_TCP_ROUTE=0
    ProfileSelected --> PselectTree: tree waiter profile
    Multicast --> SameRouteRetry: verification miss
    TCP --> SameRouteRetry: verification miss
    PselectCompact --> SameRouteRetry: verification miss
    PselectTree --> SameRouteRetry: verification miss
    SameRouteRetry --> Multicast
    SameRouteRetry --> TCP
    SameRouteRetry --> PselectCompact
    SameRouteRetry --> PselectTree
```

当前所谓TCP→pselect“回退”是启动前由 `GHOSTLOCK_TCP_ROUTE=0` 决定的静态选路。已进入TCP后，失败只重试当前路线，不会在同一进程内自动转pselect。

## 清理边界

| 范围 | 主要所有者 | 清理 | 不干净条件 |
|---|---|---|---|
| PI waiter/owner/consumer | `PiRace::start_threads/run`（`run_main_route_threads()` 调用） | `request_stop()` 原子量 + `PthreadOwner::join` | consumer 或内核 PI 路径不返回；等待无超时（`PI-TIMEOUT-01`） |
| Multicast resident | `kernel5_resident_*()` | disarm、join、socket/page cleanup | ghost/scratch未修复 |
| TCP局部资源 | `do_tcp_fake_lock_route()` | stop/join/munmap/close | PI写结果仍可能无法单凭fd清理判定 |
| pselect fd | `do_pselect_fake_lock_route()` | restore stdio + close | consumer stuck时故意保留 |
| 喷射页/mm上下文 | `util.c` | `cleanup_page_prepare_state()` | 与路线和阶段全局状态耦合 |
