# Native 全局状态矩阵

本文档记录**当前保留**的可变全局状态（CPP14 收尾口径）：一个真正的 process singleton、三个引用别名
façade、一个启动期只读上界，以及会话/路线拥有的资源。宏、编译期常量与不可变偏移表不列为状态。

## 1. 保留清单与理由

| 变量 | 定义 | 形态 | 读者/写者 | 生命周期 | 保留理由 |
|---|---|---|---|---|---|
| `g_exploit_session` | `session/exploit_session.cpp` | 对象 | 全攻击链 | 进程级；主线程初始化，运行期只读其成员 | **唯一 process singleton**：一次运行的全部会话状态（runtime / profile / addresses / heap / race / victim / parked handoff） |
| `g_target_profile` | `exploit_session.cpp:36` | **引用别名** → `g_exploit_session.profile` | 地址/payload/route/验证 | 一次解析后只读 | `profile.h` 的 `_RSO` 宏在头文件内引用；别名替换已验证会改变攻击函数布局 |
| `g_resolved_addresses` | `exploit_session.cpp:37` | **引用别名** → `g_exploit_session.addresses` | `resolved_addresses_data_alias()` 热路径 | 一次发布后只读 | 同上：被 payload/地址换算热路径直接调用 |
| `g_heap_context` | `exploit_session.cpp:38` | **引用别名** → `g_exploit_session.heap` | payload/page 热路径 | 进程级；攻击窗口由单线程驱动 | 别名替换已验证改变 `waiter_thread`（-2）与 `do_pselect_fake_lock_route`（-22），保留至专门门禁 |
| `g_direct_map_end` | `memory/address_space.cpp:12` | 标量 | `in_direct_map()`、KernelSnitch 扫描边界 | 启动期一次（`apply_iomem_cache()` 可收窄） | iomem 实测上界；发布后只读 |

> 三个引用别名是 SESSION 系列的过渡 façade：所有权自始在 `ExploitSession`，别名只为避免在敏感热路径
> 改动调用点。删除别名（直接使用 session 成员）经构建对比会改变攻击关键函数，登记为后续专门门禁项。

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
| `ks`（宏） | `util.cpp` | `g_heap_context.snitch` 别名；owner 为 `HeapContext` |

## 4. 已删除/收归历史（索引）

- 引用别名：`g_runtime_config`、`g_pi_race_context`、`page_base`/`last_mm_struct`/`fake_*`/`memfd_leak`、
  `CORE`/`mm_struct_sz()` 宏。
- 零调用 façade：`kernelsnitch()`/`kernelsnitch_param()`、`init_cpu_config`、KernelSnitch util 包装、
  8 个核心头文件与 `profile.h`/`target.h` 的 C 分支。
- 迁移期镜像：`g_core_main`/`g_core_consumer`、`p0_kernel_phys_load`/`g_init_cred_image`（S06 起冗余）。

## 5. 风险与后续

- P0：`race` 原子量与 Heap 页状态跨线程/热路径共享，任何收归/重排都需攻击函数形状对比 + 门禁。
- P1：三个引用别名的最终删除（直达 session 成员）是 CPP14 登记项，需专门门禁。
- P2：`standard_io_backup` 随 `SELECT-01`（compact Select 外层重试重建）一并处理。
