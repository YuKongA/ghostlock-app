# 三条路线、数据流与清理

## 核心维护图：三路线端到端主链

此图是解耦阶段的唯一强制更新 UML。自 S03 起，每阶段只有在构建和真机测试通过后，才把该阶段已经验证的调用边界更新到图中；未完成或仅有 TODO 的目标结构不得提前画入。S02 不追补更新。

```mermaid
flowchart TD
    Main["main()"] --> Run["run_exploit()"]
    Run --> Init["select_offsets() / init_p0_profile()"]
    Init --> W1["W1: retry_write_stage()"]
    W1 --> Write["do_one_write()"]
    Write --> Page["prepare_good_kernel_page()"]
    Page --> Payload["prepare_skb_payload()"]
    Write --> Race["run_main_route_threads()"]
    Race -. pthread .-> Waiter["waiter_thread()"]
    Race -. pthread .-> Owner["owner_thread()"]
    Race -. pthread .-> Consumer["consumer_thread()"]
    Waiter --> Choice{"route"}
    Choice --> M["Multicast"]
    Choice --> T["TCP Zerocopy"]
    Choice --> P["pselect/select"]
    W1 --> W2["W2: spawn_victim() + credential stage"]
    W2 --> W3["W3: flags/seccomp mode when needed"]
    W3 --> Root["child_main() handoff"]
```

三条路线共用profile、地址转换、堆页准备、fake对象、PI三线程和W1/W2/W3验证；只在“用哪种可控结构覆盖stale waiter”上分叉。

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

TCP是局部资源所有权最集中的路线：线程、映射和fd均经单一 `out` 路径回收。PI三线程和喷射页仍由外层公共代码管理。

## pselect/select

```mermaid
flowchart TD
    R["do_pselect_fake_lock_route()"] --> Prep["prepare_pselect_fdsets()"]
    Prep --> Shift["pselect_waiter_shift()"]
    Prep --> Word["pselect_put_waiter_word()"]
    Word --> Sets["in/out/ex fd_set"]
    R --> Open["open_selected_fds()"]
    R --> Save["reserve_standard_io()"]
    R --> Kind{"compact_waiter?"}
    Kind -->|6.1 fallback| PS["pselect()"]
    Kind -->|6.6/6.12| S["select()"]
    PS --> Frame["stack waiter layout"]
    S --> Frame
    R --> C["consumer_thread()"]
    C --> PI["PI walk / tree relink"]
    R --> Restore["restore_standard_io()"]
    R --> Check{"consumer_inflight?"}
    Check -->|no| Close["close timerfd/pipe"]
    Check -->|yes| Dirty["keep fd until process exit; step=34"]
```

consumer仍停在内核时不关闭相关fd，是为避免回收它仍在引用的对象。这个出口是dirty failure，不适合运行时切换路线。

## 堆与地址数据流

```mermaid
flowchart LR
    O["active_offsets"] --> A["init_p0_profile() / data_addr()"]
    A --> Targets["stage target addresses"]
    O --> KS["setup_kernelsnitch()"]
    KS --> Coll["futex hash collisions"]
    Coll --> MM["mm_struct candidate"]
    MM --> Page["prepare_kernel_page()"]
    Page --> Ctx["prepare/spray/pre/post mm_ctx"]
    Page --> SKB["prepare_skb_payload()"]
    Targets --> SKB
    SKB --> Fake["page_base + fake_lock/waiter/task/fops"]
    Fake --> Routes["three routes"]
    Routes --> Verify["W1/W2/W3 verify callback"]
```

## 路线选择与回退现状

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
| PI waiter/owner/consumer | `run_main_route_threads()` | stop原子量 + join | consumer或内核PI路径不返回 |
| Multicast resident | `kernel5_resident_*()` | disarm、join、socket/page cleanup | ghost/scratch未修复 |
| TCP局部资源 | `do_tcp_fake_lock_route()` | stop/join/munmap/close | PI写结果仍可能无法单凭fd清理判定 |
| pselect fd | `do_pselect_fake_lock_route()` | restore stdio + close | consumer stuck时故意保留 |
| 喷射页/mm上下文 | `util.c` | `cleanup_page_prepare_state()` | 与路线和阶段全局状态耦合 |
