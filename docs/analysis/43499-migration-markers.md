# 43499 迁移标记清单（`.disabled`）

> 目的：把"逻辑要迁到 Kotlin"的 C++ 文件标成 `<name>.disabled`，使其不被任何构建/glob 选中。
> 判据：**改名会误伤保留函数 ⇒ 说明拆得不够细**，先拆再标。
> 基线：`bb27b48a`。本轮只做文件切分与改名，不改函数内容、不改 CMake、不改攻击路径。

## 一、约定与恢复

- 标记：`mv <file> <file>.disabled`（最可逆，不删除、不改内容）。
- 恢复：`mv <file>.disabled <file>`；有依赖组的要整组恢复（见 §四）。
- 效果域：`core/*.cpp` 只被**手工 NDK dev 构建**用 glob 选中；Gradle 的
  `ghostlock43499` 只列 `gl43499/jni43499.c`，因此本清单**不改 Gradle 产物**，
  但手工 dev 入口（`main_dev`/`api_probe`）会缺符号。

## 二、已标记（13 个）

| 现文件 | 原内容 | 依赖组 |
|---|---|---|
| `api.cpp.disabled` | 旧 JNI 面 `glk43499_api_*` | G1（+`dev_profile.h`） |
| `backend.cpp.disabled` | `attack_write`（W1 编排 + 实例化） | — |
| `route.cpp.disabled` | `RouteController`（无调用方） | — |
| `dev_profile.h.disabled` | `build_dev_515_profile` | G1 |
| `race_session.cpp.disabled` | `race_run_params_for/_from_session`、`run_main_route_threads_from_session` | — |
| `route_multicast_policy.cpp.disabled` | mcast 会话适配器 + `w2_fast_repair_prebuild/activate` | G3 |
| `route_select_session.cpp.disabled` | select 会话适配器 | G3 |
| `route_tcp_session.cpp.disabled` | tcp 会话适配器 | G3（+`profile_accessors.h`） |
| `support_session.cpp.disabled` | `page_spray_inputs_for/_from_session`、`log_startup_context`、`init_p0_profile`、reclaim/prebuilt 槽策略 | G2（+`profile_accessors.h`） |
| `address_math.cpp.disabled` | `detect_target_soc`、`ResolvedAddresses::*`（地址换算） | — |
| `attack.cpp.disabled` | `install_profile`、`resolve_profile_addresses`、`log_execution_settings`、`apply_iomem_cache`、`slab_drain`、`write_root_script`、`perf_find_task` | G4（+`attack.h`） |
| `session.cpp.disabled` | `g_exploit_session` 定义、`ExploitSession` 构造、`RuntimeConfig` | G4 |
| `profile_accessors.h.disabled` | 读全局 profile 的 inline 访问器 | G2/G3 |

## 三、切分后的保留集合

`core/` 只剩：`memory.cpp`、`race.cpp`、`route_multicast.cpp`、`route_select.cpp`、
`route_tcp.cpp`、`support.cpp`、`ks_bridge.c`（+ 未被 include 的头）。

**符号闭合验证（本轮已跑）**：

```text
保留 TU 编译：6/6 通过（aarch64 -std=c++23），ks_bridge.c 通过
llvm-nm -u <保留 .o> ∩ llvm-nm --defined-only <11 个 disabled .o>  =  ∅
保留 TU 的唯一项目级未定义符号 = glk_ks_dev_*（由保留的 ks_bridge.o 定义）
```

即：保留集合不再引用任何已标记文件里的符号。

## 四、恢复依赖组

| 组 | 要一起恢复 | 原因 |
|---|---|---|
| G1 | `api.cpp` + `dev_profile.h` | api 包含 dev profile |
| G2 | `support_session.cpp` + `profile_accessors.h` | 适配器读 profile 访问器 |
| G3 | `route_*_session.cpp` + `route_multicast_policy.cpp` + `profile_accessors.h` | 适配器/策略读 profile |
| G4 | `attack.cpp` + `session.cpp`（+`attack.h`） | profile 安装与会话相互引用 |

单独恢复会编译失败，属预期的整组约束。

## 五、仍在保留 TU 里的迁移逻辑（下一轮必须拆）

| 位置 | 内容 | 为什么还留着 |
|---|---|---|
| `support.cpp` `prepare_skb_payload` + `fill_profile_cred_copy` | payload 编码 | `land_with`（保留）直接调用；要等 `land_page(final bytes)` 落地（J2） |
| `support.cpp` `prepare_good_kernel_page` | 外层"整页重来"重试 | `route_select.cpp:424` 的 `SelectStackRoute::execute` 直接调用 |
| `route_select.cpp` `SelectStackRoute::execute` | 每次 compact 重试都重喷页 | **policy 嵌在 mechanism 内**；应改为 Kotlin 逐次 `prepare/land/execute`（J3） |
| `support.cpp` `spray_begin` | 内联 KernelSnitch 调用 | placement 事务与 KS 尚未按 §八 拆开（J2） |
| `ks_bridge.c/.h` | KS 桥 | 被 `spray_begin` 调用；common 落地后删 |
| `profile.h` `session.h` `target.h` `params.h` `route.h` `support.h` `memory.h` `race.h` `attack.h` `payload_encode.h` | 混合类型/常量 | 保留代码直接 include；需按 §五 的 7 项逐步参数化后再切 |

## 六、结论

- **能拆的都拆了**：13 个文件已标记，保留集合符号自闭合。
- **拆不动的只剩 4 处**，且都在 `support/route_select` 内部：
  1. payload 编码（J2 的 `land_page` 入参）；
  2. 外层整页重试（`prepare_good_kernel_page`）；
  3. select mechanism 内嵌重喷（J3 改为 Kotlin 编排）；
  4. placement 里的 KS 调用（J2 的 `prepare_placement`）。
- 这 4 处不解决，`support.cpp` 与 `route_select.cpp` 无法整文件标记。

## 七、route class 里还剩什么（J3 必须拆）

`route_*.h` 的 `*Route` **不是产品 route**，但它现在是旧四段生命周期的载体。
按字段分：

| 字段 | 类别 | 去向 |
|---|---|---|
| `socket_fd` / `pipe_read/pipe_write` / `block` / `high_read` / `client/server/punch_fd` / `mapping` / `punch_worker` / punch atomics | 物理资源 | Actor handle（保留） |
| `FdSet ×6` / `selected_fds_installed` / `block_borrows_pipe` / `consumer_stuck` | 物理/内核交互状态 | Actor handle（保留） |
| `in`（`MulticastInputs`/`SelectInputs`/`TcpInputs`） | 每调用入参 | 改为 `execute_actor` 参数，不存 |
| `status` | 每调用结果 | 回执 |
| `attempts_used` / `stamp_result` / `stamp_errno` / `calls` / `successes` / `select_result` / `select_errno` / `route_won` | 每调用结果 | 回执 |
| `prepare()/execute()/disarm()/destroy()` + `run_route_lifecycle`（`route.h:288`） | 调用顺序 policy | **删除**；由 `prepare_actor/execute_actor/close_actor` 三步承担 |

目标形态（每个机制）：

```c
struct McastMechanism { ghostlock::support::UniqueFd socket; };   /* Actor 持有 */
int32_t mcast_prepare(McastMechanism&, const glk_primitive&, glk_call_result&);
int32_t mcast_execute(McastMechanism&, const glk_primitive&, glk_call_result&); /* 窗口 */
void    mcast_close(McastMechanism&);
```

- `prepare` 只建资源；`execute` 只跑 plant→arm→disarm；`close` 只关 fd。
- 窗口内语句顺序与 legacy 逐条一致（`MCAST_BLOCK_SOURCE` 拷贝与 `consumer_go`
  之间零 syscall）；这是攻击路径改动，需要真机门禁，属 J3。

**为什么还没做**：这是函数签名/窗口代码改动，不是文件改名；按 G0 约束
（不改 CMake、不进 J1–J3）本轮只把它写成清单，不落代码。
