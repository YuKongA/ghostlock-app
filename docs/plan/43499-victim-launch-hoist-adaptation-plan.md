# 43499 victim 启动机制上浮公用机制：适配 L 设计 v2（D0 依赖/三态/capability 事实）

- 状态：**L 设计 v2（2026-10-28，task-85），只写文档+approval append、零实现、不 commit**；v1 被 critic 判不可（B1–B3+I1–I4），本件按 Lead 裁定修订，待二轮复核。
- 范围：43499 自有 victim 启动（Kotlin ProcessBuilder shell + JNI posix_spawn helper 双路径）如何适配公用机制；不改攻击/finish/W3/G05 语义；不碰 98260 文件；不写实现。
- 依据：当前树（未 commit）`ProcessVictim.kt`/`ProcessVictimSession.kt`/`Cve43499JniVictim.kt`/`Cve43499Lifecycle.kt`/`Flow43499Impl.kt`；公用雏形 `core/victim/VictimScriptLauncher.kt`、`core/victim/VictimSession.kt`；98260 R1e 已用 `VictimScriptLauncher`（`Cve98260RemoteExecutor.kt:169`）。

## 0. 结论摘要

1. **B3 事实更正**：`core/victim/VictimCapability.kt` 实为 **bits 0–9**（sha256 `cf6b50fd…`，3,415 B）：0–6 批 A + **7/8/9=PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM**（task-78 append；`:38/:41/:44`）；**无 `EXEC_SCRIPT_NO_KILL`、bit7 不被占用**；G04 早期稿 8/9/10 已由 Lead 改 7/8/9 并落地。位表属共享 core、全局 owner 冻结；98260 若需 NO_KILL 只能 append **10+**（禁重排/复用 7）；43499 spec 只用符号常量。
2. **B1 已改（不写既成契约）**：`VictimLauncher`/`VictimLaunch` 仅为**D0 依赖项**——由全局 owner 冻结公共 API；43499 只写适配边界（输入 spec facts/输出 session 以 D0 为准）。候选 A=`launch(spec): VictimResult<VictimSession>`+facts evidence；B=`VictimLaunch{session,identity,pid}`——**两者并列待 owner 裁决**，不选一个当契约；A1 只写 fake/不接线，A2 等 D0 冻结 sha。
3. **B2 三态矩阵**：`injection.victim = process | jni-spawn | common`；**E1+E7 前全部可用、默认保持现状（process/jni-spawn）**；`common` opt-in；A2 只“新增 common+可选”，**不禁用/不删旧路径**；旧路径 deprecated 但可显式选择；默认切 common 需 E1+E7+一轮回归；删除旧路径另批新授权。
4. **I4**：A2 拆两步——①新增 common/公共 session（默认不变）；②旧路径 deprecated 但可显式启用；删除另批；task-78/80 append 命令**走公共 session，不依赖旧 ProcessVictim**。
5. **I2**：静态 gate 两组——默认/新路径**零违规**；兼容路径白名单（ProcessVictim @Deprecated+证明不能默认选中；FinishExecution 以“无生产调用”判）；加负例证明兼容路径非默认。
6. **I3 裁定分级**：Q1/Q3/Q6=**Lead 暂定可执行**；Q2/Q4/Q5=**user/全局 owner 裁决**。
7. **I1**：D0 记录 owner 名、冻结 sha/版本、验收项位置；冻结前 A1 只 fake、A2 挂起；`VictimScriptLauncher` 当前仅 script carrier，**carrier 语义覆盖列为 D0 需求**。
8. **真机**：E1（W1-only）/E7（W3 hard gate）未跑⇒迁移后仍 UNVERIFIED；不标 supported。

## 1. 现状盘点（file:line）

| 项 | 位置 | 事实 |
|---|---|---|
| ProcessVictim（默认） | `ProcessVictim.kt:54-72`（ProcessBuilder）；`:191-220` line protocol；`ProcessVictimSession.kt:26/120/179/232/277/285/293/306`；生命周期 `Cve43499Lifecycle.kt:226` 默认 `{ ProcessVictimSession() }`、`:251` start、`:440-458` rooted detach | 子进程 shell；finish/park/bye；rooted detach；task-78/80 status/comm append（`ProcessVictim.kt:107-153`，`VictimProtocolAppend.kt`）；ProcessBuilder 系非零 fork 语义 |
| JNI helper（J 主路径） | `Cve43499JniVictim.kt:88/150/158-168/185/229/269`、`:31` helper 名、`:39` limits；JNI `jni43499.c:1473` spawnVictim；Native `core/victim_protocol.c:356` glk_victim_spawn（socketpair:374、posix_spawn:427、fd3/4、close_range/assert） | helper PIE；fd 协议 3/4；posix_spawn 零 fork；raw memfd；capability 按位；park/bye/detach |
| 创建/持有/回收 | lifecycle `:251-262`；closeAll `:440-458`（active→prebuilt→victim，rooted 只 detach）；Flow `runSpec.victimFactory`（`Flow43499RunSpec.kt:110`、`Flow43499Impl.kt:187-193`）；一 run 一 session | 自持；`dispose`；E4 fd 泄漏=0 |
| task-45 | `Flow43499Impl.kt:1482` finish step、`:1548` finishExecuteViaVictim（rooted execScript，无 App/su）；`ProcessVictimSession.kt:179/232`；lifecycle rooted detach | finish/park/rooted detach 冻结；G04 B1 授权仅 append-only 命令，不含删除/改语义 |
| G04 append | `core/victim/VictimSession.kt:404/419/438` probeStatus/probeCommSet/probeComm；`VictimCapability.kt:38/41/44` bits 7/8/9；`VictimProtocol.kt:110-118` opcode 6/7/8；helper `victim_helper.c:975-981` dispatch | task-78/80 已实现；W3 硬门依赖；公共 session 必须保留 |
| 公用雏形 | `core/victim/VictimScriptLauncher.kt:21/37`（script carrier）；98260 `Cve98260RemoteExecutor.kt:169` 已用；`core/victim/VictimSession.kt:17` config | 上浮方向；当前 script-only、未覆盖 43499 carrier 全量（D0 需求） |
| 错误/fd/capability | `VictimError.kt`+`VictimResult`；`victim_protocol.h:118-125` 错误码；fd `{0,1,2,3,4}`；`VictimCapability.kt:15-44` bits 0–9 | 共享 core 最终由全局 owner 冻结；43499 不写死位数 |

## 2. 目标公用机制（D0 依赖，非既成契约）

- **D0（全局 owner 冻结）**：公共 `VictimLauncher`/launch spec/`VictimSession` surface、失败码、fd/零 fork 不变量、capability 位表、carrier 覆盖（script/carrier）、生命周期/close/detach 合同；记录 **owner 名 + 冻结 sha/版本 + 验收项位置**。**冻结前不得把任一形态当契约**。
- **I3（必选验收项）**：D0 验收**必须**含公共 `VictimSession.probeStatus/probeCommSet/probeComm`（`core/victim/VictimSession.kt:404/419/438`，task-78/80 append）；A1 fake/contract 必须断言三方法；缺任一⇒该路径 **FAIL**、A2a **不接**（不得静默 fallback）。
- **候选 A**：`launch(spec): VictimResult<VictimSession>` + backend facts 走 evidence；session 自持 close/detach。
- **候选 B**：`launch(spec): VictimResult<VictimLaunch>`，`VictimLaunch{session, identity, pid}`。
- 两者并列待 **user/全局 owner** 裁决（Q2/Q4）；adapter 只按 D0 冻结面接线。
- **共同预期**：`VictimLaunchSpec` 含载体 path/argv/env、capabilities（符号）、raw size、timeouts、**killPolicy**（killOnTimeout/killOnClose/detachOnTimeout 或等价）：43499 non-rooted close 可 kill helper、rooted detach/park 不 kill；98260 NO_KILL（close 不 kill 已 detach payload、timeout 只关等待窗口、must_reboot）；返回 session；失败码 `VictimError`；fd `{0,1,2,3,4}`；零 fork；内部 posix_spawn/service 由公用层决定；无 backend 常量；probe 三方法可见。**D0 未放入公共 `VictimSessionConfig` 时，43499 用 backend `Cve43499VictimKillPolicy` 表达、D0 冻结后映射**。

### 2.1 两类 carrier 需求差异

| 维度 | 43499（本 backend） | 98260（另一 backend） |
|---|---|---|
| 载体/域 | app 域长驻 helper（nativeLibraryDir PIE，posix_spawn 零 fork），**非 script-detach** | **script-detach payload**（`VictimScriptLauncher`：start→execScript→onScriptDone→close；payload 后台 detach）；UserService/shell uid2000/u:r:shell:s0，AIDL |
| 生命周期 | **W1 permissive 后启动**→W1b 预建→W2 cred root→W3→finish rooted `execScript`→**rooted park/detach 不 kill** | **armed once/boot**；触发后**不得 kill/timeout/reap payload**；失败/超时/成功均 `must_reboot=true`；finish fake-cred posix_spawn；acceptance service |
| 能力 | PROBE_UID/SPAWN_HELPER/PERF_SELF_SAMPLE/EXEC_SCRIPT/PARK/BYE/DETACH_ROOTED/PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM | core 类型/错误/fd；需 **`EXEC_SCRIPT_NO_KILL`+`PARK`+`DETACH_ROOTED`**（或由 script/carrier 语义承载，公共 spec 给字段）；+log fd/AIDL；不需 43499 perf/comm marker |
| 关键依赖 | **perf self-sample(pid=0)**（app 跨任务拒；W1 permissive 后可用）；**comm 15B marker=leaf oracle**；**无用户态 artifact（payload=FinishScript）** | service 域/uid、日志 fd、`Cve98260FinishScript` 脚本；payload 自持页/park |
| 失败语义 | W2 dirty/retained⇒**W2 fail-stop（G01）+冷重启**；W3 失败禁 finish/acceptance | 不得 kill/timeout/reap payload；失败/超时/成功均 `must_reboot=true` |
| 共性/公共要求 | core 类型/Result/Error/Identity/Capability/fd {0..4}/零 fork/机制-only；append-only；backend 给 spec/facts；**killPolicy 见 §2**（43499 non-rooted close 可 kill helper/rooted detach/park 不 kill；98260 NO_KILL）；公共 launcher 支持两类 carrier、kill 策略可配置；**43499 不并入 `VictimScriptLauncher`** |

- **结论（D0/Q5 输入）**：公共 launcher/spec 须支持**两类 carrier**；carrier kind/spec 字段/能力集/超时/close/kill 策略可配置；**43499 不并入 `VictimScriptLauncher`**。


## 3. 43499 适配方案

| 项 | 内容 |
|---|---|
| A1 spec adapter（fake） | `Cve43499VictimLaunchSpecFactory`（`:21-30`）：helperPath=nativeLibraryDir helper；argv/env 当前；raw=perf 窗口（`Limits` 校验）；capabilities 符号常量 **10 项**：PROBE_UID/**SPAWN_HELPER**/PERF_SELF_SAMPLE/EXEC_SCRIPT/PARK/BYE/DETACH_ROOTED/PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM（`start()` 强制）；timeouts 来自 limits；**killPolicy=`Cve43499VictimKillPolicy`**（non-rooted close 可 kill helper；rooted detach/park 不 kill）。**A1 可进 provisional（不切默认）**；fake/contract 断言 10 项+killPolicy 字段/语义；D0 冻结后映射公共 spec |
| A2a 新增 common | **可进 provisional（不切默认）**；等 D0 冻结 sha 后：`Cve43499JniVictimRuntime` 经公共 launcher（不直接构造 `core.VictimSession`）；`injection.victim=common` opt-in；默认不变；公共 session 暴露 append 探针 |
| A2b 旧路径 deprecated | `ProcessVictim`/`ProcessVictimSession` **不禁用/不删除**，标 @Deprecated、可显式选；**默认保持 ProcessVictim；common 非默认由 gate② 负例证明；旧路径非默认在 Q6/切换批**；删除另批（Q3） |
| 保留 backend 专属 | finish 选择/`FinishHandoff`/A1 victim-only（`Flow:1482/1548`）；W3 status/comm 探针（公共 session）；perf 参数/投票/2-of-3/raw；uid/rooted/park/detach 策略/evidence；G04/G05 不变 |
| append 兼容 | task-78/80 经公共 session（不依赖旧 ProcessVictim）；公共 API 缺某命令⇒该路径 FAIL，不静默 fallback、不阻断 A1 |
| 三态矩阵 | 见 §3.1 |

### 3.1 `injection.victim` 三态矩阵（B2）

| 值 | E1+E7 前 | 默认 | A2 后 | 删除/退役 |
|---|---|---|---|---|
| `process` | 可用（现状） | 现状默认（debug 配置） | deprecated，可显式选 | 删除另批新授权（Q3） |
| `jni-spawn` | 可用（现状） | 现状可选 | deprecated，可显式选 | 同上 |
| `common` | 不存在/无 | — | **新增、opt-in**；默认不变 | 切默认需 E1+E7+一轮回归（Q6） |

## 4. 依赖/写范围/批序

| 阶段 | 内容 | 写范围/门 |
|---|---|---|
| **D0** | 全局 owner 冻结公共 API（候选 A/B 裁决、capability 0–9 冻结/10+ append 规则、carrier 覆盖、失败码/fd/零 fork）；记录 owner/冻结 sha/验收位置 | 只读；未冻结⇒A1 fake only、A2 挂起 |
| **A0** | HEAD+工作树 hash、provisional-baseline（status/plan/approval/98260 plan）、旧路径行为基线 | 只读 |
| **A1** | `Cve43499VictimLaunchSpecFactory.kt`（NEW）+ fake launcher/contract；不接线 | 43499 专属；core 只读 |
| **A2a** | 公共 launcher 接线（`Cve43499JniVictim.kt`）、`injection.victim=common` 可选；默认不变 | 43499 专属；D0 冻结后 |
| **A2b** | 旧路径 @Deprecated（不移除/不禁用默认）；静态 gate 两组 | task-45 文件仅 deprecated 注释/不动语义（需 Lead 确认范围） |
| **A3** | 回归/门禁/默认切换决策（Q6）/删除批另立（Q3） | 按批 |

- **共享边界**：`core/victim/*`、JNI、`victim_protocol`、`VictimProtocol.kt` 由全局 owner/公用批；43499 若需编辑→**停并报 Lead**；不碰 98260 文件；task-81/82 运行期间只写本设计。
- **I1**：D0 必须给 owner/冻结 sha/验收项位置；未给前 A1 只 fake、A2 挂起；`VictimScriptLauncher` carrier 覆盖（script 之外的 carrier）列入 D0 需求。

## 5. 测试/门禁（I2 两组静态 gate）

| 类 | 内容 |
|---|---|
| fake launcher/contract | fake A/B 形态（按 D0 最终面）；spec 校验（path/argv/symbolic capability/raw/timeouts）；失败码 `VictimError`；capability 未声明拒绝；fd `{0..4}`；park/detach/close/status/comm；append 命令走公共 session |
| 静态 gate ①（common/新路径） | `tools/verify_43499_victim_launch_hoist.py`：common/新路径**零违规**（禁 ProcessBuilder/Runtime.exec/直接 JNI spawnVictim/自持 `core.VictimSession(`）；A2a/A2b 阶段各自独立结果 |
| 静态 gate ②（默认组白名单） | 默认组白名单=ProcessVictim（**@Deprecated，default until Q6**）；**负例证明 common 未被默认选中**（debug 默认值/工厂断言）；“证明旧路径非默认”移到 **Q6/默认切换批**；FinishExecution 以“无生产调用”判；A2a **不切默认**（E1/E7 前禁止）；白名单变更需 Lead/owner 批准 |
| 回归（清单必须通过） | finish victim-only（A1）、W3 硬门（G04 task-78/80）、G05 close/retained、旧路径显式选择、rooted detach；fake/Flow 测试；**I2 回归清单全绿** |
| 离线 | compile/UTest 过滤、NDK 若零改动仅核对、scanner r2、verify_jni_exports、ABI probe、zero-fork gate、diff-check；provisional-baseline 记录 |
| 真机 | E1/E7 未跑⇒UNVERIFIED、不标 supported；`common` 需 E1+E7 |

## 6. 风险/TD/回滚

| 项 | 说明/缓解 |
|---|---|
| 双路径 | process/jni-spawn/common 三态；漂移风险；fake contract+同 evidence；common opt-in；TD 旧路径删除（Lead） |
| ProcessVictim 兼容 | task-45 finish/park/detach 冻结；**不删除/不禁用**；默认切换前须 E1+E7+回归；删除另批 |
| task-45 授权 | G04 B1 仅 append-only；A2b 仅 deprecated 注释（Lead 已批）；删除需新授权 |
| 98260 共享 core | capability 位表/协议共享；98260 NO_KILL 只能 append 10+，不得复用 7；冲突⇒停报 Lead |
| provisional-baseline | A0 冻结；A1/A2 逐批比对；不宣称稳定 |
| D0 延迟 | A1 fake 可先行；A2 挂起；冻结物变更需重签/重跑 |
| 回滚 | `injection.victim` 切回旧值；旧路径代码保留；公共 launcher 失败不静默回退（FAIL） |

## 7. 裁定分级（I3）

| # | 问题 | 归属/结论 |
|---|---|---|
| **I2 授权** | **Lead 已批准** A2b 对 `ProcessVictim`/`ProcessVictimSession` 仅加 `@Deprecated`/KDoc 注释与文档，**不改既有 finish/park/bye/rooted detach 语义/错误码/耗时**；回归清单必须通过；**删除仍另批 Q3** | **已授权（A2b 注释/文档 only）** |
| **I3 D0** | D0 必选验收=公共 session `probeStatus/probeCommSet/probeComm`（`:404/419/438`）；A1 fake 断言三方法；缺任一⇒路径 FAIL/A2a 不接 | **已登记（D0 验收项）** |
| Q1 | ProcessVictim 兼容期 | **Lead 暂定**：至 E1+E7+回归，common opt-in 先行 |
| Q3 | 删除 task-45 文件授权（A2b/删除） | **Lead 暂定可执行**：A2b 仅 deprecated；删除另批新授权 |
| Q6 | Q6 common 切默认 | **Lead 暂定**：D0+E1+E7+回归后另批 |
| Q2 | 公共 API 最小面（A/B、facts） | **user/owner 裁决** |
| Q4 | capability 位表（0–9 冻结、98260 append 10+、禁重排） | **user/owner 裁决** |
| Q5 | 公共 launcher 是否并入 script-launch 形态 | **user 结论：43499 不并入 `VictimScriptLauncher`；两类 carrier 列为 D0 需求** |

## 8. UNVERIFIED / 不做

- **UNVERIFIED**：D0 API/owner、候选 A/B、capability 位表未定；`VictimScriptLauncher` 仅 script；E1/E7/⑥ 未跑；旧 ProcessVictim/公共 launcher 真机；task-45 删除未批；迁移后 finish/W3/G05 真机；NO_KILL append 10+ 未发生。
- **不做**：不写实现、不改生产代码/共享文件、不碰 98260、不改 task-45 语义（仅 deprecated）、不 commit、不标 supported；本件只写 43499 适配边界与 D0 依赖，不自定公共契约。
