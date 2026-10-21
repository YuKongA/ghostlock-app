# 43499 native 设备动作计划（v8 · legacy under kt-step）

> **2026-10-20 kt-step 目标**：生产路径改为 [43499-kt-step-plan.md](43499-kt-step-plan.md)（全部逻辑 Kotlin、C++ 仅薄 syscall）。本文件的 N1b/N2/N3 C++ 逻辑路径降 **legacy**，不接入生产；N1a 快照字段保留为诊断。

- 日期：2026-10-20
- 级别：L（攻击关键路径 + 跨 Native/Kotlin 契约 + 资源生命周期）
- 关系：服务 [43499-production-engine-plan.md](43499-production-engine-plan.md) 的 K2/K3/K4；K1 已实现（Kotlin 引擎 + 端口 + 能力门，28 个单测）。
- 状态：**legacy under kt-step**。N1a 已实现（快照字段）；N1b native form 路径已实现但**降 legacy，不接入生产**；N2/N3 与真机门禁不再按本计划推进。

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件是 **legacy** 记录（N1b/N2/N3 C++ 逻辑），**不接入生产**（MASTER-PLAN §13）。

## 0. 范围裁定记录（P6 边界，待用户确认）

`43284-production-engine-plan.md` P6 写「43499 接入（只写 Kotlin）」、DoD 写「43499 仅 Kotlin 接入」。2026-10-20 用户目标「把43499实现接入生产级引擎」是更晚、更直接的指令；K1 的取证表明真机路径停在 native 缺口（快照无代、form 不传播、access 未装配、五个设备槽为 null）。

**记录（待确认）**：native 工作越过 P6「只写 Kotlin」。本计划**不自行生效**；用户明确批准后，才把 P6 原文划掉并写沿革/代价/技术债/回滚。未确认前 N1b–N3 均不写码；K1 保持现状，43499 真机路径停在 K1 的具名拒绝。

**2026-10-20 Round 3 例外（N1a）**：只做了快照三代字段的编组与 host 测试。它不触及攻击路径、不改变设备行为、不新增 backend 逻辑；目的只是让 K1 的 `generations` 能力门能读到 native 已经拥有的事实（评审 C1）。

**2026-10-20 Round 4（N1b native 路径）**：把 `WordWrite43499.form` 映射为 `WriteRequest.mode/preserve`，经 `device_write_43499` → `Access43499Parameters` → `KernelAccess43499::write` → `WordLanding` → `Window` → `RouteExec` 携带；旧窗口/旧 route 重载保留，默认路径不变。未加 JNI 能力探针、未改 K1 的 false 门、未跑真机门禁；N1b 仍与 N2/N3 同一设备批，未过门前不宣称通过。

## 1. 现状（取证）

| 缺口 | 位置 | 证据 |
|---|---|---|
| 快照无三代 generation | `core/src/boundary/protocol/marshal_43499.hpp:17-22` | 13 字段；`Facts43499` 的三代（`api_43499.hpp:99-101`）未编组 |
| form 在 typed 路径丢失 | `api_43499_ops.cpp:24-37`；`api_43499.hpp:67-75` | `land_43499` 只把 value 变 8 字节；`map_write_form` 无调用者 |
| access 工厂未接线 | `access_factory_43499.cpp:18-23` | multicast 返回 `MulticastWaiterAccess<NoRouteExec>`；initialize 必拒 |
| 五个设备槽为 null | `api_43499_ops.cpp:59-64` | prepare/discover/spawn/victim_probe/finish 未赋值 |
| route 固定 target/value | `landing/multicast_route.hpp:89-130` | `execute` 对不等 target/value 直接失败；不能一个 access 连写三步 |
| 旧 victim 已删除 | `6f7ded87:src/core/backend/victim/victim_process.cpp`（287 行） | 只读参考；含命令协议/seccomp/leaf/handoff，不能整段照搬 |

## 2. 关键决定（回应 v2 S1–S8）

### D1 参数单一权威：`Init43499`（S1/S2）

- `Init43499` 已有 route/CPU/stamp/limits/fakeTask/fakeLock 等字段；**不再新增 `StepAssembly` 或第二份 17 数组**。
- 唯一初始化入口（写死，不留“或”）：新 JNI `initializeAccess43499(handle, Init43499, route, offset, offsetSet, target, value, form, out: LongArray, refusalOut): Int`；`out[0]` 是**新分配的 accessId**（>0），失败时 0。没有独立 `makeAccess`；ID 由本入口分配并返回，调用方保存后传给 shutdown/destroy。
- **入口校验（T6）**：先 `has_room(out, 1)`；`out` 为空/长度 <1 返回 `bad-parameters:out-array`，**不分配 slot/wired holder、不触 native**；补空数组/短数组用例。
- 该入口从 DTO 读 geometry/limits/fakeTask/fakeLock；`target/value/form` 是本步的 route 守卫与写形状。`initialize43499Full` 的裸数组入口删除；`null/<字段>/多余字段` 一律具名拒绝，删除 `fill_access_params_43499_full` 的静默 return/忽略。
- `TargetState.fake_task/fake_lock` 来源 = `Init43499`（不是 prepare/discover）；`mm_struct/page_base` 来源 = discover/prepare；生命周期表见 §4。

### D2 每步输入：`WordWrite43499`（S2/S7）

- 每步写请求唯一来源 = `WordWrite43499 { address, value, form, offset, generations }`。
- `target` 就是 `address`，route 守卫对 = `(address, value)`；K1 的 `KernelStep` 已强制 `target == address`，native 不另存 target。
- 新增 `form` 到 `device_write_43499` / `write43499` / `KernelAccess43499::write` 的参数面；`KernelAccess43499::write` 用 `map_write_form` 得到 `mode/preserve`，构造 `WriteRequest{target=address, mode, preserve_child}`，交给 `WordLanding::land_word(address, value, request, offset)`。
- `MulticastLanding` / `MulticastWindow` / `RouteExec` 的 `execute` 带上 `WriteRequest`；`MulticastRouteConfig.target/value` 从 `(address,value)` 得到，guard 不再另存一份。
- `write43499`（access 级）在 K1/K2 零调用，标 legacy；K2 只用 `writeWord`（会话级，带三代与 form 校验）+ 每步 access 事务。

### D3 每步一次访问事务（S2/C2）

- `writeStep` 顺序固定：`initializeAccess43499(init, route, offset, address, value, form, out)`（本调用**分配 accessId、构造** wired access 并 bring_up）→ 保存 `out[0]` → `writeWord` → `shutdownAccess(accessId)` → `destroyAccess(accessId)`；不再有独立 `makeAccess`。所有出口用同一个 accessId。
- 初始化时就拿到本步的 `(target=address,value,form)`，route config 在 `bring_up` 前完整；`writeWord` 用同一组值。
- 所有出口（成功/拒绝/异常/取消）都必须 shutdown→destroy；access 只在本次事务内存在。
- multicast 具体类型 = `MulticastWaiterAccess<MulticastRoute<LinuxRouteOps>>`（v1 写错为 `<LinuxRouteOps>`）。

### D4 页与 route 的每步准备

- prepare 产出 `page_base/word0/word1`；discover 产出 `alias_delta/found/mm_struct`；`payload_base = alias_delta + page`（按有符号 delta 计算，照旧实现）。
- 每步写前用 `build_payload_page`/`prepare_page` 生成该步的页；页内容、route 配置、写请求三者用同一 `(address,value,form)`，不允许“页按 A 建、route 按 B 跑”。
- `WordsRegion`、`TargetState`、`MulticastRoute`、`MulticastWindow`、`MulticastLanding`、access 各一个 owner；释放顺序 = 写结束 → access shutdown → access destroy → route/window/landing → words close。

### D5 设备槽（S4/S6）

- `prepare`：`Prepare43499Values` → `steps::PrepareRequest` → `run_prepare`；`WordsRegion`/`TargetState` 收进 session 设备 context，不落栈。
- `discover`：`Discover43499Values` → `steps::DiscoveryRequest` → `run_discovery`；校验 prepare 代。
- `spawn_victim`：只保留 fork/pipe/perf/task/probe/kill/wait；命令协议/seccomp/leaf/handoff 不在 N3。
- **probe 通道**：spawn 建 `cmd` 与 `value` 两条 pipe；victim 循环读 1 字节：`C` → 读自身 `/proc/self/comm`，`U` → 读 `/proc/self/status` 的 Uid 行；`victim_probe` 写命令并读回（comm 字符串/uid u64）。子进程在 cred 被改写前继承 pipe fd，改写后仍可读自身 `/proc/self/*` 并写 pipe；父进程只读 pipe，不要求 root。命令未知/EOF/超时/短读全部具名拒绝。**kind 映射固定：kind 0 → `U`，kind 1 → `C`**；两条 pipe 在成功/失败/超时/EOF/取消所有出口关闭。
- `finish_victim`：**只做 retire/park**；`handoff` 在 N3 具名拒绝 `unsupported:handoff-deferred`。

### D6 能力探针（S3）

- K1 的 `NativeCapabilities.formPropagation/perStepTargetValue` 没有探针，保守 false。
- N1b 增加一个显式 JNI 能力探针（`Native43499.capabilityProbe(): LongArray`，发布 version + generation/form/target 三个布尔）；N2 的 Kotlin 端口改为读它；没有探针的环境仍 false。

## 3. 批次（每批一类事；攻击路径批必须真机门禁）

| 批 | 内容 | 门禁 |
|---|---|---|
| N1a | **已实现**：快照加三代字段（`marshal_43499.hpp` names/fill）；新增 host `marshal_43499_test` 断言名字/值；CMake 注册 | `nativeTestHost` **75/75**、`nativeBuildAndroid` 绿；离线 |
| N1b | **native 路径已实现**：`device_write_43499` 加 mode/preserve；`KernelAccess43499` → `WriteRequest`；`WordLanding/Window/RouteExec` 带 form；`write43499` 保持 legacy 零调用。**JNI 能力探针 + K1 接线在 N2** | 攻击路径：**N1b+N2+N3 同一真机门禁批**，冷机、固定 CPU 对、单 route；未过门禁不进 N4 |
| N2 | access 装配 + 每步事务 + 页准备 + route 类型修正；Init 单权威；**Kotlin 端口扩展** | 同上 |
| N3 | 五个设备槽：prepare/discover/spawn/probe/finish（retire/park；handoff 拒绝） | 同上 |
| N4 | 端到端 w1→w2→w3 + `victim uid=0`；日志归档 | `docs/analysis/device-gates/43499-*.md` ≤8 KB |

**N2 Kotlin 改动（S3/T1/T2）**：`Native43499.kt` 新增 `initializeAccess43499`（out 返回 accessId）/`capabilityProbe` 的 `external fun`；`AndroidNative43499Api` 增加对应委托；`Native43499Api`（新增 `capabilityProbe/initializeAccess/writeWord/shutdownAccess/destroyAccess`，无 `makeAccess`）、`JniBackend43499Port`（能力门读探针；`writeStep` 保存 `out[0]` 并在所有出口 shutdown/destroy）、对应 `JniBackend43499PortTest`/`KernelMemoryEngineTest`。`KernelMemoryEngine`/`KernelMemoryRun`/`Facts43499` 的语义不变。

**Assembled43499 处置（T1）**：N2 删除孤儿 `Assembled43499.kt`（无外部调用方）与 `Native43499.initialize43499Full/initialize43499` 声明；恢复清单 = `git show 92d55d3e:app/src/main/kotlin/com/ghostlock/app/jni/cve43499/Assembled43499.kt`。若删除与其它批冲突，则保留一个只调用 `initializeAccess43499` 的兼容 shim，并在 N2 测试断言零调用旧入口。

**N2 access 构造点（T2）**：`device_initialize_access_43499` 分配 slot/accessId，调用新 `create_wired_access_43499(route, params, target)` 构造 `WiredAccess43499`（持有 `MulticastRoute<LinuxRouteOps>` + `MulticastWindow` + `MulticastLanding` + `MulticastWaiterAccess`），把 `g_ops_ctx_43499` 指向本次 access，再 `initialize`；成功把 accessId 写回 `out[0]`。`device_make_access_43499` 删除/标 legacy；`destroyAccess` reset holder 时按 access→route/window/landing 顺序释放。

**执行入口**：`app/src/androidTest` 的 `KernelMemoryEngineInstrumentedTest` 直接注入输入调用 K1 引擎；不新增 UI/配置页。命令走现有 instrument/`deviceGate` 等价入口。

**N1b/N2/N3 的离线部分**：可先写代码并用 fake route/host 测试审形状，但**不得在真机门前宣称通过**；真机门禁是唯一功能判据。

## 4. 所有权与生命周期（AGENTS §5）

| 对象 | 创建 | 所有者 | 借用者 | 终结点 | 释放者 |
|---|---|---|---|---|---|
| session handle | JNI create | engine | port | close | port |
| access | writeStep | port（事务内） | writeWord | shutdown→destroy | port |
| WordsRegion | prepare | session device context | prepare/step | N4 后 close | context |
| TargetState | prepare/discover + Init(fake) | session context | access/window | N4 后 | context |
| MulticastRoute/Window/Landing | writeStep | port（事务内） | access | access destroy 后 | port |
| victim | spawn | engine | probe/finish | finish retire/park | engine |
| page/fragment | writeStep | port（事务内） | route | 事务出口 | port |

**单会话不变量（S4 选定）**：`g_ops_ctx_43499` 是单会话全局；K2 强制**同时活跃 session ≤1**，第二个 `create` 返回 `unsupported:session-busy` 并附测试；不选“每 session context”（留作未来扩展点）。access 先 shutdown 再 destroy；words/target 在 access 之后释放；重复 close 幂等；失败/取消不留下半接线。

## 5. 验证矩阵

| 层 | 输入 | 预期 |
|---|---|---|
| N1a host | 13 字段旧表 / 16 字段新表 | 三代名字与值全部编组；旧调用方短数组具名拒绝 |
| N1b host | form 1/2/3、0/4；`cve43499_kernel_access_test`/`access_chain_test`/`multicast_landing_test`/`multicast_window_test` | mode/preserve 映射到 `WriteRequest` 并在 landing/window/route 可读；无 shape 在 access 前拒绝；非法 form 不触设备 |
| N2 host fake route | 同一 access 两步不同 target/value | 第二步用新事务成功；旧事务已 shutdown/destroy；无 UAF |
| N2 Kotlin | capabilityProbe 有无 / 第二会话 / 每步 initialize→destroy / out 空或短 | 无探针 false；第二 create 具名 busy；accessId 非零且只在本步使用；所有出口 shutdown→destroy；out 空/短返回 bad-parameters:out-array 且不分配 |
| N3 Android | prepare/discover 失败/成功 | 具名推进；`page_base/found` 有值或具名拒绝 |
| N3 真机 | spawn/probe/finish，kind 0/1，pipe 超时/EOF | uid/comm 真值；retire 后进程消失；handoff 拒绝；所有出口 pipe 已关 |
| N4 真机 | 全链 | `victim uid=0`；W1/W3 form 读数；日志归档 |

## 6. 明确保留

- 不恢复旧 `main.cpp`、Lua、CLI、`TypedBackendProbe`。
- 不把旧 victim 命令协议/seccomp/leaf/handoff 原样搬进 native；N3 只做 spawn/probe/finish(retire/park)。
- 不新增统一 Session 抽象；不改 43284 StageEngine。
- 不改 UI/配置/profile；K1 的 `KernelMemoryEngine`/`KernelMemoryRun`/`Facts43499` 语义不变，只有 N2 的端口适配器扩展。

## 7. 风险与未决

| # | 风险 | 处置 |
|---|---|---|
| R1 | 页/route/写请求三者不一致 | 用同一 `(address,value,form)` 构造；host fake route 测两步不同 target/value |
| R2 | 单会话全局 ctx 被第二个 session 覆盖 | 已选：同时活跃 ≤1，第二个 create 具名 busy；补测试 |
| R3 | victim handoff/seccomp/leaf 范围膨胀 | N3 只做 spawn/probe/finish(retire/park)；其余列技术债 |
| R4 | 无真机可用 | N1b–N4 保持未验证；只合入 N1a 前先跑 host 门禁；N1a 也待范围确认 |
| R5 | 能力探针缺失时误开 | 没有探针一律 false（K1 已如此） |
