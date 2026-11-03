# CVE-2026-98260 backend 适配项目公用结构（L 计划 v2.1）

> 日期 2026-10-28。级别：L（结构/跨端契约/攻击关键路径）。
> 关系：本件是 [98260-backend-plan.md](98260-backend-plan.md) **结构适配补编**（rev4 方向）；指针落账**尚未完成**（主计划余 263 B 需先压缩，见 §8；实现批前落账），不推翻安全不变量。
> 基线（树身份）：`dev-nocpp` @ `a9af5482` + 未提交 B1–B3 + 在途 task-32 混合工作树（dirty 227）；R1 门禁在最终树或隔离 worktree 跑。既有 5 红项=`PayloadRequestsTest`(3)/`ScriptRenderTest`(2)（owner=在途 finish 批次，证据见 B4 gate §3）。
> 用户要求（2026-10-28）：「将其改造适配为本项目的结构，包括公用 victim，log，payload，finish，flow funner」。
> v2 变更：**R2 移出本批准位；按 critic S-01/S-02 重做 finish/victim 设计；flow runner 改 `BackendSpec.runner`**；逐条响应见 §0。

## 0. 评审响应（v1 → v2；对 design-critic 2026-10-28 报告）

| 编号 | 状态 | v2 改法 |
|---|---|---|
| **S-01 阻断** finish | 已改 | 98260 脚本改由 **`Cve98260FinishScript`** 从 profile 生成；共享 `FinishScript/FinishState` 不承载 98260 参数；新增独立 **`Cve98260FinishAcceptance`**（无 43499 binding/resourcesReleased）；证据由 service 经 AIDL `verify()` 回传，app 只装配；`FinishAcceptance` 原样+43499 回归；AIDL 契约写死；R1d=攻击路径，重跑 B1 门禁+真机知情确认 |
| **S-02 阻断** victim | 已改 | R1 port 直接复用 `core/victim` 的 `VictimResult/VictimError/VictimIdentity/VictimIdentityReader/VictimCapability`；删 backend 重复 `VictimIdentity`（机械迁移）；append-only `EXEC_SCRIPT_NO_KILL`（不声明 `EXEC_SCRIPT`）；spawn fd 纪律/fd 泄漏=0；临时 seam TD（R2 落地即删）；R2 移出（§5） |
| S-03 重要 runner | 已改 | 用 **`BackendSpec.runner: BackendFlowRunner?`**（spec 单点声明，编译期强制新增 FLOW_RUN backend 实现接口成员）；dispatcher=`spec.runner?.run(...) ?: Blocked`；每次 `run` 新建 Flow；43499 parity（Refused/exception 前缀/cancel）测试 |
| S-04 重要 log | 已改 | fd 生命周期写死（app `Os.dup`→PFD→AIDL→close；service attach 后 close PFD/raw fd；destroy `attach(-1)`）；payload 行经 service JNI `gl_log` 写 app fd（NATIVE）；终端走 FlowProgress；失败回退=AIDL onLog→app `log`（SHELL）；**G14** 真机 gate+fd 泄漏单测 |
| S-05 重要 攻击路径 | 已改 | R1d 单列攻击路径变更：重跑 B1 native 门禁/payload 参数矩阵并更新 `98260-b1-gate-*`；真机 finish gate pending+用户知情确认；声明只改 finish 脚本来源/执行入口，不改 trigger/leak/fake-cred、不改变 park 时序 |
| S-06 重要 物证行号 | 已改 | 引用改 `victim_helper.c:619`（超时 SIGKILL）/`:693`（错误清理） |
| S-07 重要 R2 | 已改 | R2 **移出本计划**，不列入本批准位；另立 L plan（R1 通过+有设备后），须先做 fd3/4 冲突/协议线程-UAF 窗口分析、G14 定义、UserService 加载 victim JNI 验证（见 §5） |
| S-08 重要 流程/配额 | 已改（R-07：指针待落账） | 指针目标=主计划头/§11/§13/§14 + `REMAINING-PLAN.md`；**当前未实际写入**（主计划余 263 B，需先压缩一小段），列为实现批前置（§8）；树身份/5 红项已写 |
| S-09 重要 payload 命名 | 已改 | 改名 **`Cve98260ExploitPayload`**（与项目 finish payload 不同义，KDoc 明示）；sha/身份只在 RequestFactory/readiness 快照缓存计算，`rows()` 只读缓存 |
| S-10 建议 origin | 已改 | 映射表：service native fd 行=`NATIVE`；payload 行经 service JNI gl_log→NATIVE；fallback app 行=SHELL；终端=FlowProgress；一行一次 write/ASCII/禁密钥 |
| S-11 建议 文件清单 | 已改 | §6 每批列精确文件:节（AIDL 方法码/CMake target/proguard/strings）与依赖顺序 |

## 1. 目标 / 范围 / 非目标

**目标**：保留攻击语义与安全不变量，把 98260 实现归位到项目五块结构：公用 victim（R1=语义/类型/接口复用；执行载体仍 payload）、log（fd+格式+origin）、payload（exploit artifact 声明）、finish（脚本生成/交接/证据合同）、flow runner（spec 单点 runner）。

**非目标**：不改 exploit 原语（trigger/leak/fake-cred）；不换 Shizuku UserService 承载；不改 `BootSafetyStore` 唯一权威；不 commit；**R2 不在本批准位**。

**安全不变量（不变）**：触发后任何路径不 kill/timeout/reap payload；armed 后失败/超时与成功都 `must_reboot=true`；同 boot 单次 armed；journal 顺序；成功判据不靠日志文本。

## 2. 现状 → 目标（五块）

| 结构 | 现状 | 目标（R1） |
|---|---|---|
| flow runner | `BackendRunDispatcher` 内 `when(spec.id)` 分支 | `BackendSpec.runner` 单点声明；dispatcher 只调 `spec.runner`；43499/98260 各自实现 `BackendFlowRunner`（把 factory+Flow 调用搬入；runner 每 run 新建 Flow；异常前缀留 runner） |
| log | AIDL onLog 仅进终端；service 无日志 fd | app 日志 fd 经 AIDL `attachLog` 到 service；JNI `gl_log_attach_fd`；service/payload 行经 `gl_log_write` 写 app fd（NATIVE）；destroy detach；fallback SHELL |
| payload | 仅 APK 内 artifact，无项目声明/证据 | `Cve98260ExploitPayload`（artifact：path/sha/size/argv；KDoc 明示≠finish payload）；RequestFactory 校验并进 evidence；rows 读缓存快照 |
| finish | ksud 脚本硬编码在 payload；验收仅 Flow 内判 | 脚本=`Cve98260FinishScript`（profile 生成）；`prepare` 传 script+runId；service 暂存→payload `--finish-script/--run-id`→fake-cred worker 执行；验收=`Cve98260FinishAcceptance`（service 读证据） |
| victim | service 自有 executor/JNI | `Cve98260VictimPort` **复用 core victim 类型/语义**（无 kill；capability 声明）；spawn fd 纪律；临时 seam（TD-R1-4，R2 偿） |

## 3. 边界物证（不换通用 helper 的原因）

1. 通用 helper `execScript` 超时路径 `kill(child,SIGKILL)`（`victim_helper.c:619`；错误清理 `:693`）⇒ 不能把 payload 交给 helper 的 execScript/close 管理（违反「触发后不 kill」）。
2. helper 为 app uid/域（app `posix_spawn`），98260 必须 uid 2000/u:r:shell:s0（UserService 承载，用户裁定）。
3. payload 需 exec-self（`/proc/self/exe wait`）+ 独立会话 + 持页 park；helper 封闭能力集无 payload 载具语义。
4. 可复用点：core victim 的类型/结果/失败码/会话操作语义/fd 纪律/机制-only 分层；payload 的 `child dup2(g2,3)/dup2(s2,4)`（`timer.c:1522-1536`）与 victim 协议 fd3/4（`VictimProtocol.kt:29,32`）**冲突**，R2 必须先解决。

## 4. 决策（D1–D5）

- **D1 flow runner**：`ui/backend/BackendFlowRunner.kt`（接口 `id: BackendId`、`suspend fun run(context,onLine): BackendRunUiState`）；`BackendSpec.runner: BackendFlowRunner? = null`（43499/98260 override；43284/Custom null）；`BackendRunDispatcher.dispatch`=`spec.runner?.run(appContext,onLine) ?: Blocked(no_flow_entry)`；runner 每 run 新建 Flow；43499 异常前缀保持 `cve43499-run-execution:exception:`；测试：每个 FLOW_RUN spec 有 runner、runner.id∈Backends、NO_FLOW_RUN=null、43499 Refused/前缀/cancel parity。
- **D2 log**：AIDL `attachLog(in ParcelFileDescriptor logFd)`（新方法码=11）；app `Os.dup(LogFile.fd)`→PFD→发送后 close dup+PFD；service `detachFd()`→JNI `attachLogFd`（`gl_log_attach_fd(fd, BuildConfig.DEBUG, Info)` 内部 dup+CLOEXEC）→close PFD；destroy `gl_log_attach_fd(-1)`；payload 行由 service 读存量 → `Native98260Jni.logLine(level,line)`（gl_log→NATIVE）；终端仍 AIDL onLog→FlowProgress；attach 失败回退：service 不写 fd，AIDL onLog→app `log` SHELL origin；G14 真机 gate；单测：格式/一行一次/fd 泄漏（service attach-detach 后 `/proc/self/fd` 计数复原）。
- **D3 payload**：`backend/cve98260/payload/Cve98260ExploitPayload`（`name/path/sha256/size/argv`；KDoc：exploit artifact，**不是** finish payload）；`Cve98260RequestFactory` 存在/大小/sha 校验并进 evidence；`BackendCve202698260.rows()` 只读 readiness 快照里的 artifact 身份（不热路径算 sha）；真机门禁校验 sha 与离线一致。
- **D4 finish**：① 脚本唯一生成点=`Cve98260FinishScript.render(profile, finishConfig?)`，使用 profile 的 `manager_package/kmi/late_load_args(含--allow-shell)/ksud_fallbacks` + marker/runId 约定；**不写** 43499 的 `/dev/dfm6` 假设，98260 marker 路径由本后端定义（`/data/local/tmp/glk98260.finish.ok|fail`，内容=runId，仅 service 可读）；② AIDL `prepare(profileHex, payload, finishScript: byte[], runId: String)`（方法码不变；service 写 `/data/local/tmp/glk98260-finish.sh` 0700）；③ payload 删内嵌 ksud 脚本（`timer.c:922-967`），fake-cred worker 以 `posix_spawn("/system/bin/sh", script)` 执行（**只 kill 脚本子进程、绝不 kill payload**；`FinishExecutionEvidence.timedOut` 语义对齐）；`--finish-script/--run-id` argv，env `GHOSTLOCK_RUN_ID`；④ 验收=`Cve98260FinishAcceptance`：script sha(service 重算==app 传值) + execution exit/timeout/runId + ksud 证据(/data/adb/ksu/bin/ksud 等候选，service 有界 `--version`) + uid marker==0 + SELinux enforce==1 + ok marker 内容==runId 且 fail marker 不存在；**不含** 43499 binding primitive 地址/resourcesReleased；app 只装配 service `verify()` 回传字段。⑤ `FinishAcceptance`/`FinishScript` 原样不动（43499 回归测试）；共享仅限 runId/marker 机制约定。
- **D5 victim（R1）**：`backend/cve98260/victim/Cve98260VictimPort` 直接 import `core/victim` 的 `VictimResult/VictimError/VictimIdentity/VictimIdentityReader/VictimCapability`；ops=`start/probeUid/execFinishScript/park/bye/detach/close`（命名对齐 core；**no-kill**）；`core/victim/VictimCapability.kt` +append-only `EXEC_SCRIPT_NO_KILL`（R1 port 仅声明它，不声明 `EXEC_SCRIPT`）；删除 `backend/cve43499/victim/VictimIdentity` 重复定义→import core（机械；`FinishExecutionEvidence.kt:3` 同步）；`jni98260.c` spawn 加 closefrom/close_range（保留 0/1/2/日志 fd），payload fd 集检查 + fd 泄漏=0 测试；port 为**临时 seam**（TD-R1-4，R2 落地即删）。

## 5. R2（不在本批准位，另立 L plan）

R2=payload 实现 victim 协议 fd3/4 + service 用 `core/victim.VictimSession` 驱动。前置：① fd3/4 冲突分析（payload child `dup2(g2,3)/dup2(s2,4)` vs `VictimProtocol.kt:29,32`）；② 协议线程/UAF 窗口/持页 park 论证；③ UserService 进程加载 `gl43499` victim JNI 验证（TD-R2-1）；④ 新 G14 定义（本计划只定义 log G14，序号需与主计划确认）；⑤ 冷机真机 G8–G14+。R1 的 TD-R1-4 偿还在 R2 完成时。

## 6. 批次（精确文件；依赖顺序 R1a→R1c→R1e→R1b→R1d）

> **2026-10-28 执行状态**：R1a/R1c/R1e/R1b/R1d **离线完成**（记录 [R1 progress](../analysis/98260-r1-progress-20261028.md)）；已落实用户裁定：**payload 由公用 victim 启动、finish 由 rooted victim 执行**；spawnStatic 退出生产（TD-R1-5）；真机 G8–G14 pending。

| 批 | 文件（新增/修改:节） | 验收 |
|---|---|---|
| **R1a** flow runner | NEW `ui/backend/BackendFlowRunner.kt`; MOD `ui/screen/module/backends/BackendSpec.kt`(+runner), `BackendCve43499.kt`(+runner), `BackendCve202698260.kt`(+runner), `ui/backend/BackendRunGate.kt`(dispatcher 去分支), `app/src/test/.../FlowRequestTest.kt`(+runner parity) | compile+全量单测（仅 5 既有红）；新增 parity 测试 |
| **R1c** artifact | NEW `backend/cve98260/payload/Cve98260ExploitPayload.kt`; MOD `ui/.../Cve98260RequestFactory.kt`, `BackendCve202698260.kt`(rows 快照), `data/cve98260/Cve98260ReadinessProbe.kt`(artifact 身份) | 单测：缺失/超限/sha 不匹配 fail-closed；rows 不计算 sha |
| **R1e** victim | NEW `backend/cve98260/victim/Cve98260VictimPort.kt`; MOD `core/victim/VictimCapability.kt`(+NO_KILL), `backend/cve43499/victim/VictimProbe.kt`(删重复 Identity), `backend/cve43499/finish/FinishExecutionEvidence.kt`(import), `cve98260/service/Cve98260RemoteExecutor.kt`, `cve98260/service/Cve98260UserService.kt`, `cpp/gl98260/jni98260.c`(fd 纪律), `jni/Native98260Jni.kt` | 单测：ops 语义/无 kill/capability；fd 泄漏=0（宿主 fake）；native 0 warning |
| **R1b** log | MOD `aidl/.../ICve98260UserService.aidl`(+attachLog=11), `client/Cve98260ServiceClient.kt`(dup/PFD), `service/Cve98260RemoteExecutor.kt`+`Cve98260UserService.kt`, `cpp/gl98260/jni98260.c`(+attachLogFd/logLine), `jni/Native98260Jni.kt`, `ui/backend/BackendFlowRunner.kt`(98260 runner), `log/log.kt`(SHELL fallback helper), `res/values{,-zh-rCN}/strings.xml`(G14 文案) | compile/assemble；单测：fd 生命周期/格式/origin/无 secret；G14 真机 pending |
| **R1d** finish（攻击路径） | NEW `backend/cve98260/finish/*`; MOD `aidl`/client/service、`timer.c`（删内嵌脚本；rooted victim exec finish）、`params`(argv)、tests；B1 gate 重跑 | 离线：脚本渲染/验收矩阵/参数拒绝；B1 门禁重跑；**真机 finish gate pending+知情确认** |

**离线门禁**：clean→externalNativeBuildDebug（0 warning）→compileDebugKotlin→assembleDebug→全量单测（XML 三桶）。
**真机**：R1b G14、R1d finish、R1 整体仍 `device-gate-pending`（无设备）；未过不得 supported。

## 7. 风险 / TD

- **TD-R1-1** finish 执行角色仍 payload（非公用 victim）；偿还=R2 完成。
- **TD-R1-2** `Cve98260FinishAcceptance` 与 43499 合同差异（无 binding primitive 地址/resourcesReleased）已显式登记，不得到 43499 合同放宽。
- **TD-R1-3** log fd 跨 uid（app→UserService）依赖 PFD 权限；回退=D2 SHELL 路径；真机 G14 覆盖。
- **TD-R1-4** `Cve98260VictimPort` 为临时 seam（审批通过后存在，R2 完成删除）；R2 前不得在它上面加后端语义。
- **TD-R2-1**（R2）UserService 加载 victim JNI/fd3/4 冲突待验证。
- **沿革**：本计划守则/AGENTS 无强制越权；R1d 攻击路径门禁 pending 需用户知情确认（device-gate-pending 已确认口径延用）。
- **配额**：本计划 ≤16 KiB；指针写入主计划（余 263 B 以上需压缩一小段后写入）；delta/附录 B 仅必要关系行，先压旧沿革。

## 8. 批准位、残留关闭表与指针

- **design-critic 复评（2026-10-28）**：v2 总判 **可进实现（阻断 0）**；S-01/S-02 核心闭合，R2 已移出；复评报告落账 `architecture-review-log.md`。
- **用户批准（2026-10-28）**：R1a→R1c→R1e→R1b **+ R1d** 全部批准；R2 另立计划。R1 离线实现完成，真机 pending。
- **实现批内必须逐项关闭的残留**（critic R-01～R-12；关闭后才算对应批完成）：
  - R1a 前：R-01 runner 去默认 null（或 sealed Flow 能力，补 43284/Custom 显式 null）、R-09 树身份/证据同步口径；
  - R1e 前：R-04 capability no-kill 消费/测试、`VictimIdentity` 全量迁移文件+测试、fd file-actions 顺序与 fd 集快照；
  - R1b 前：R-05 raw fd 关闭/fallback native sink、R-06 G14 定义（log fd；R2 用 G15）；
  - R1d 前：R-02 脚本步骤表（ksud/SELinux/uid/ok-fail marker）、worker exit/timeout/runId marker、R-03 中性本地 evidence DTO（不跨用 43499）、ksud `--version` 执行机制、R-10 timeout 只杀脚本 child 用例、R-11 binder 上限重算；
  - R1d/R1e 前：R-08 §6 文件清单补 Flow/Request/Factory/迁移测试；
  - 批准/实现前：R-07 指针实际落账（主计划压缩一段后写“补编指针+批次行”，`REMAINING-PLAN.md` 索引行）。
- **指针方案**：主计划头（关系行）+ §11（R1a–R1d 批次/写范围）+ §13（批准记录）+ §14（进度）；`REMAINING-PLAN.md` 索引行；delta 余 83 B 不写（由主计划兜底）。
- **验证状态口径**：R1 全部 `device-gate-pending`（G14/R1d finish），未过真机不得 supported。
- **强制确认沿革**：debug-only profile、device-gate-pending（沿用主计划 §13）。
