# Native 函数标准化说明

每个条目依次记录“用途”、“状态/输入输出”和“调用/清理”。系统调用只在影响资源所有权或控制流时列出。

> 2026-09-18 刷新：主线已按 `main` 薄适配 + `ghostlock::ops/race/stages/victim/route/support/memory` 分层组织
> （CPP12 批次 A/B + CPP13 类化）。符号所有权以 `native-global-state.md`、`native-cpp-current-uml.md`
> 与 `native-cpp-migration-plan.md` 为准。

## `main.cpp`（薄适配）

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `main()` | 解析 `--profile`，取进程级 `g_exploit_session` 调 `run_exploit()` | argc/argv → exit code | 无资源 |
| `run_exploit()` | 阶段编排：setup → phase-1 probe → W1 → W2/W3 chain → handoff | session + profile → exit code | 委派 `ghostlock::stages::*`；phase-1 probe 走 `resident_route().start()/stop()` |

## `exploit_ops.cpp`（`ghostlock::ops`）

### 配置、环境与地址

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `validate_offsets_profile()` | 校验公共与路线偏移 | `kernel_offsets*` → bool | 只读 |
| `log_execution_settings()` | 输出执行参数诊断 | profile | 无资源 |
| `resolve_profile_addresses()` | 发布结构化地址快照 | 写 `g_resolved_addresses` | `resolved_addresses_init()` |
| `select_offsets()` | 按 `uname -r` 选外部/内置 profile | 写 `g_target_profile` | 成功后发布地址 |
| `apply_iomem_cache()` | 以 home 的 iomem dump 收窄 direct map 末端 | 写 `g_direct_map_end` | 文件在函数内关闭 |
| `write_root_script()` | 生成 root/KernelSU 调度脚本 | 读 runtime paths，写文件 | 句柄在函数内关闭 |
| `perf_find_task()` | 由 perf 观测取得 victim `task_struct` | 无 → address | perf fd/ring 局部回收 |

### 攻击写入与堆前处理

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `do_one_write()` | 一次抽象内核写；5.x resident 特例经 `kernel5_resident_*` wrapper | target/mode → bool | `prepare_good_kernel_page()` → `run_main_route_threads()` |
| `slab_drain()` | 攻击前轻量 fork 波次降低旧 slab 影响 | 无 | `std::array<ChildProcess,64>` 在函数内回收 |
| `check_selinux_off()` / `enforce_readable()` | SELinux 状态读取 | 无 → bool | `UniqueFd` 局部关闭 |
| `process_has_seccomp()` | 解析当前进程 Seccomp 状态 | `/proc/self/status` → bool | `FILE*` 关闭 |

## `routes/route_threads.cpp`（`ghostlock::race`）

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `waiter_thread()` | PI waiter：LOCK chain futex → 等 owner → `WAIT_REQUEUE_PI` → 路线执行 → multicast ghost disarm | race 原子量 | futex；退出前 disarm |
| `owner_thread()` | 持有 target/chain PI futex | race 原子量 | 退出前 unlock |
| `consumer_thread()` | 另一 CPU 上调 nice 触发 PI walk（失败时 futex 备用刺激） | race 原子量 | `sched_setattr_tid()` |
| `reset_main_route_state()` | 重置一次竞争（保留 `fast_repair` latch） | `PiRace::reset()` | 无资源 |
| `PiRace::run()` | 等就绪 → `CMP_REQUEUE_PI` → 等 `route_done`，合并计数器 | `RouteStatus` | 无资源 |
| `run_main_route_threads()` | 创建、同步、触发并 join 三线程 | `WriteRequest*` → bool | `PthreadOwner` 拥有线程 |

## `session/exploit_stages.cpp`（`ghostlock::stages`）

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `run_setup_stage()` | profile 加载、运行时配置、root 脚本、启动日志、pre-W1 drain | profile_path → StageResult | — |
| `run_w1_stage()` | W1（SELinux）与 W1b scratch repair、resident policycap 修复 | session → StageResult | 调 `retry_write_stage`/`ops::do_one_write` |
| `retry_write_stage()` | 通用 stage 重试 + 验证 + W2 fast-repair 预构造 | stage/verify → bool | — |
| `park_retry_child()` | W3 轮次间把 rooted child 停靠到 session（pid 经 `VictimContext::release_child()` 转移） | session/chain → void | — |
| `root_victim()` | spawn + vivo tag 清理 + W2 cred 写 | chain → VictimRound | 未 rooted 时 `release_child()` 后 kill+reap |
| `clear_victim_seccomp()` | W3 TIF_SECCOMP/mode 写 + probe；丢失 child 时 `mark_child_exited()` | chain → bool | — |
| `run_w2_w3_chain()` | W2/W3 重试链 | session/chain → StageResult | — |
| `run_handoff_stage()` | settle、root shell 交接、KernelSU 探针、resident stop | session/chain → StageResult | handoff 后 `release_child()` |

## `session/victim_process.cpp`（`ghostlock::victim`）

| 函数 | 用途 | 状态/输入输出 | 调用与清理 |
|---|---|---|---|
| `child_main()` | victim 协议：报告 task/UID、探针与最终交接 | `VictimContext*` | 关闭未用 pipe 端；可 fork/exec |
| `spawn_child()` | 建管道 + fork + `p->set_child()` | `VictimContext*` → PID | child 分支调 `child_main()` |
| `spawn_victim()` | spawn + 读回 task 地址 | `VictimContext*` → PID | 关 task read 端 |
| `verify_selinux_stage()` / `verify_w2_stage()` / `verify_seccomp_probe_stage()` / `verify_leaf_dir_stage()` | 各 stage 验证回调 | context → bool | 经 pipe 命令 victim |

## `session/victim_context.hpp` / `.cpp`（`ghostlock::VictimContext`）

| 成员 | 用途 |
|---|---|
| 六个 `UniqueFd` | victim 协议管道端（子侧在 fork child 内关闭） |
| `child()` / `set_child()` / `mark_child_exited()` / `release_child()` / `retire_child()` | pid 显式所有权：作用域退出绝不 signal；退休 = SIGKILL + reap，停靠/交接 = 转移 |

## `routes/multicast_waiter_route.hpp` / `.cpp`（`MulticastWaiterRoute`，CPP13）

| 成员 | 用途 |
|---|---|
| `init()`（inline，host-safe） | 重置 resident 上下文（字段、原子量、status） |
| `start()` | 装填 resident：两 worker + socket、`CMP_REQUEUE_PI`、SIGUSR1、等待 ready 后 adjust |
| `write()` | 发布 target/value，等 worker 重喷，再 adjust 触发 PI walk |
| `stop()` | disarm（`stop_requested` + 等 consumer idle）→ destroy（join、close）→ session 释放 Heap |
| `multicast_waiter_worker()` / `multicast_owner_worker()`（static） | resident waiter（stamp/re-spray）与 owner（PI 链） |
| `multicast_waiter_stamp()`（static） | 经 `ghostlock::encode_multicast_waiter` 生成 waiter words + erase words/family |
| `resident_route()` | 进程级实例访问器（`route_operations.cpp` wrapper 转发） |

## `routes/route_operations.cpp`（路线实现与 wrapper）

| 函数 | 用途 | 状态/输入输出 | 调用/清理 |
|---|---|---|---|
| `kernel5_resident_start()` / `kernel5_resident_write()` / `kernel5_resident_stop()` | resident 的 thin wrapper → `resident_route()` | 同类方法 | 无额外资源 |
| `do_kernel5_fake_lock_route()` | one-shot Multicast：VLA stamp（`encode_multicast_waiter`）+ `setsockopt(MCAST_BLOCK_SOURCE)` + consumer 触发 + drain + close；`EADDRNOTAVAIL` 为预期路径（stamp 已落地） | `WriteRequest*` → RouteStatus | 局部 fd 关闭 |
| `tcp_wait_for_consumer_idle()` / `tcp_make_pair()` / `tcp_punch_thread()` | TCP zerocopy 辅助（consumer drain、loopback 对、memfd 打孔） | — | `UniqueFd` 拥有 |
| `TcpZerocopyRoute::prepare/execute/disarm/destroy()` / `do_tcp_fake_lock_route()` | TCP 路线（定义在 `route_operations.cpp`） | 写 route/consumer/punch 状态 | dirty 时保留资源至进程退出 |
| `fdset_*` / `pselect_*` / `open_selected_fds()` / `reserve_standard_io()` / `select_stack_build_fdsets()` | Select stack 辅助（fd_set word、fd 布置、stdio 备份） | — | 路线/进程收尾 |
| `SelectStackRoute::prepare/execute/disarm/destroy()` / `do_pselect_fake_lock_route()` | Select 路线（定义在 `route_operations.cpp`） | 写 route/consumer 状态 | consumer stuck 时保留全部 fd |

## 其余单元（函数名不变，所有权见全局矩阵）

- `memory/*`：`resolved_addresses_*`、`detect_target_soc()`、`PayloadPage`/`HeapContext`/`MmContextSet`、`encode_*_waiter`/`payload_write_layout()`/`prepare_*_payload()`。
- `support/*`：`UniqueFd`/`MappedRegion`/`PthreadOwner`/`ChildProcess`/`Result`，`futex_op()`/`put64()`/`put32()`。
- `kernelsnitch/*`、`futex_hash.h`、`utils.h`/`timeutils.h`：碰撞发现、hash、系统 helper（清单同上一版；
  零调用 `kernelsnitch()`/`kernelsnitch_param()` 已在 `e9b8151` 删除）。
- `offsets_json.cpp`：JSON profile 解析（条目同上一版）。
- `util.cpp`：`runtime_elapsed_ms()`、`runtime_config_snapshot()`、`log_*`、`runtime_config_init()`。

## 主要工程结论

- `main` 只做解析与编排；stage 状态经 `VictimChain`/`ExploitSession` 显式传递，victim pid 归 `VictimContext`。
- PI 竞争（`ghostlock::race`）与三路线分离：resident Multicast 用类生命周期，one-shot 保持专用小栈帧与 VLA；
  payload 编码对两条 Multicast 路径同源。
- `prepare_kernel_page()`/`prepare_skb_payload()` 仍是最强堆耦合；`g_heap_context`/`g_resolved_addresses`/
  `g_target_profile` 是保留的 process singleton（理由见全局矩阵）。
- 攻击关键函数（8 个）在每次布局改动时用构建对比脚本验证 strict 或已知注解差异。
