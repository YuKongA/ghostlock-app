# Native 全局状态矩阵

本文档记录**当前保留**的可变全局状态（CPP17 收尾口径）：一个真正的 process singleton、一个启动期只读
上界，以及会话/路线拥有的资源。宏、编译期常量与不可变偏移表不列为状态。

## 1. 保留清单与理由

| 变量 | 定义 | 形态 | 读者/写者 | 生命周期 | 保留理由 |
|---|---|---|---|---|---|
| `g_exploit_session` | `session/exploit_session.cpp` | 对象 | 全攻击链 | 进程级；主线程初始化，运行期只读其成员 | **唯一 process singleton**：一次运行的全部会话状态（runtime / profile / addresses / heap / race / victim / parked handoff） |
| `g_direct_map_end` | `memory/address_space.cpp:12` | 标量 | `in_direct_map()`、KernelSnitch 扫描边界 | 启动期一次（`apply_iomem_cache()` 可收窄） | iomem 实测上界；发布后只读 |

> 三个引用别名（`g_target_profile`/`g_resolved_addresses`/`g_heap_context`）已于 CPP17 收归：调用点直达
> `g_exploit_session` 成员，`PROFILE_VALUES`/`_RSO` 宏改用 `g_exploit_session.profile`，`common.h` 引入
> session 头。构建 `55863311…` 经真机门禁（`CPP17-20260918-multicast-pass*`）成为新基线。

## 2. 会话拥有的资源（非全局）

- `ExploitSession`：`runtime`（值类型；`runtime_config_init()` 后经 `runtime_config_snapshot()` 只读）、
  `PiRace race`（futex/原子量/三线程 `PthreadOwner`）、`VictimContext victim`（六 `UniqueFd` + pid）、
  `parked_victim`/`parked_victim_cmd`、`release_resident_heap()`。
- `HeapContext`：`MmContextSet`×4、`unique_ptr skb_buffer`、`PayloadPage`（current/prebuilt/quarantine）、
  `ChildProcess leak_child`、`UniqueFd leak_memfd`、`snitch`（`KernelSnitchOwner`）。
- 路线实例：`TcpZerocopyRoute`、`SelectStackRoute`（栈上 move-only，显式 prepare/execute/disarm/destroy）；
  `MulticastWaiterRoute`（进程级实例在 `multicast_waiter_route.cpp` 匿名 namespace，唯一经 `resident_route()` 到达）。

## 3. 路线级 static（受限例外）

| 变量 | 位置 | 说明 |
|---|---|---|
| `multicast_resident_route` | `multicast_waiter_route.cpp`（匿名 ns） | resident 单例；`stop()` 后资源归零，经访问器唯一到达 |
| `standard_io_backup[3]` | `route_operations.cpp`（static） | Select 的 stdio 借用备份；构造时转 `BorrowedFd`，从不关闭 |
| `ks`（宏） | `util.cpp` | `g_exploit_session.heap.snitch` 别名；owner 为 `HeapContext` |

## 4. 已删除/收归历史（索引）

- 引用别名：`g_runtime_config`、`g_pi_race_context`、`g_target_profile`/`g_resolved_addresses`/`g_heap_context`
  （CPP17 收归，`55863311…` 门禁）、`page_base`/`last_mm_struct`/`fake_*`/`memfd_leak`、`CORE`/`mm_struct_sz()` 宏。
- 零调用 façade：`kernelsnitch()`/`kernelsnitch_param()`、`init_cpu_config`、KernelSnitch util 包装、
  8 个核心头文件与 `profile.h`/`target.h` 的 C 分支。
- 迁移期镜像：`g_core_main`/`g_core_consumer`、`p0_kernel_phys_load`/`g_init_cred_image`（S06 起冗余）。

## 5. 风险与后续

- P0：`race` 原子量与 Heap 页状态跨线程/热路径共享，任何收归/重排都需攻击函数形状对比 + 门禁。
- P1：三个引用别名已收归（CPP17）；后续新代码不得再引入进程级可变别名，只经 session 成员访问。
- P2：`standard_io_backup` 随 `SELECT-01`（compact Select 外层重试重建）一并处理。
