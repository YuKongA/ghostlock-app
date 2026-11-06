# 归档说明

- 原路径：`docs/plan/kotlin-core-observability-cleanup-annex-a.md`
- 归档日期：2026-11-04 19:35:20 -0500
- 归档原因：所属 compatibility-first KC 计划已被后续需求模型取代；附件中的旧 wire/parity/执行模型不再是活动实施账本。
- 来源：2026-11-04 未提交工作树草案；正文保留为早期冻结清单与评审沿革。现行需求入口为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Kotlin core 边界与可观测性收敛计划 — 附件 A（已取代）

> 状态：**曾由现已归档的 [`20261104-1935-kotlin-core-backend-vertical-rewrite-plan.md`](20261104-1935-kotlin-core-backend-vertical-rewrite-plan.md) 取代，不得据本文实施**。本附件只保留旧 compatibility-first 草案的冻结清单与评审沿革。
>
> 冻结基线：`dev-nocpp` / `21d9f53c00ee76891a993c8a2ecea1ea6f9be830` / 2026-11-04。
>
> 用途：把主计划里的“删除、迁移、wire、所有权、测试增量”变成可机械执行的 exact set，防止实现阶段临时扩范围或为过门修改判据。

## A1. KC1 dead-island exact set

### A1.1 删除的 production 文件（18）

1. `app/src/main/kotlin/com/ghostlock/app/core/NativeSession.kt`
2. `app/src/main/kotlin/com/ghostlock/app/core/SessionPhase.kt`
3. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessCapabilities.kt`
4. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessCapability.kt`
5. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessIo.kt`
6. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessResult.kt`
7. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessSession.kt`
8. `app/src/main/kotlin/com/ghostlock/app/core/access/AccessSessionFactory.kt`
9. `app/src/main/kotlin/com/ghostlock/app/core/access/InitializationReceipt.kt`
10. `app/src/main/kotlin/com/ghostlock/app/core/access/KernelMemoryAccess.kt`
11. `app/src/main/kotlin/com/ghostlock/app/core/access/PageCacheAccess.kt`
12. `app/src/main/kotlin/com/ghostlock/app/core/access/SessionSnapshot.kt`
13. `app/src/main/kotlin/com/ghostlock/app/core/access/ShutdownReceipt.kt`
14. `app/src/main/kotlin/com/ghostlock/app/core/nativeabi/NativeCallResult.kt`
15. `app/src/main/kotlin/com/ghostlock/app/core/nativehandle/NativeHandlePort.kt`
16. `app/src/main/kotlin/com/ghostlock/app/core/nativehandle/NativeHandleState.kt`
17. `app/src/main/kotlin/com/ghostlock/app/core/nativehandle/OwnedNativeHandle.kt`
18. `app/src/main/kotlin/com/ghostlock/app/core/nativehandle/RunResourceScope.kt`

`app/src/main/kotlin/com/ghostlock/app/core/victim/VictimScriptLauncher.kt` **不在删除集**。它当前 production caller 为 0，但 98260 的 script-detach 需求、现有类合同和专属测试仍把它定义为保留机制；KC1 只把其 disposition 从 `FROZEN` 改为 `KEEP`，不得借 dead-island 批删除。

### A1.2 删除的专属测试（3 文件 / 9 cases）

1. `app/src/test/kotlin/com/ghostlock/app/core/access/AccessIoTest.kt`：3 cases。
2. `app/src/test/kotlin/com/ghostlock/app/core/nativeabi/NativeCallResultTest.kt`：1 case。
3. `app/src/test/kotlin/com/ghostlock/app/core/nativehandle/OwnedNativeHandleTest.kt`：5 cases。

### A1.3 symbol 与外部契约负证据

KC1 的 scanner 输入固定为 A1.1 文件定义的下列类型：

`NativeSession`、`SessionPhase`、`AccessCapabilities`、`AccessCapability`、`KernelReadRequest`、`KernelWriteRequest`、`PageCacheReadRequest`、`PageCacheWriteRequest`、`AccessWriteReceipt`、`AccessResult`、`AccessSession`、`AccessSessionFactory`、`InitializationReceipt`、`KernelMemoryAccess`、`PageCacheAccess`、`SessionSnapshot`、`ShutdownReceipt`、`NativeCallStatus`、`NativeEffect`、`NativeCallResult`、`NativeHandlePort`、`NativeHandleId`、`NativeHandlePhase`、`OwnedNativeHandle`、`RunScopeToken`、`RunResourceScope`。

删除前的唯一复核必须同时证明：

- A1.1 集合以外的 `app/src/main/**` production caller 为 0；
- AIDL、Android manifest、consumer/proguard keep、JNI export/name、reflection symbol、native target manifest 引用为 0；
- `app/src/test/resources/architecture/kotlin-file-actions-v1.tsv` 对 A1.1/A1.2 的 exact rows 同批改为 `ABSENT`，且 `VictimScriptLauncher.kt`/test 保持 `KEEP`；
- `docs/development/introduction.md`、`docs/development/full-process-uml.md`、`docs/plan/execution-architecture.md` 的当前权威叙述同批改为“脚手架已退役；以后若重引入先建立真实 caller 与 owner”，历史文档只加后继指针、不改写历史事实。

任一非清单 production caller 出现即停止 KC1；禁止扩大删除集或为使 scanner 通过而改名。

## A2. Flow reflection ABI 与 Android codec

### A2.1 必须保留的 ABI

下列内容在 KC 全程保持原 package/name，不迁移：

- `com.ghostlock.app.core.flow.FlowEntry`
- `com.ghostlock.app.core.flow.FlowHostEnv`
- `com.ghostlock.app.core.flow.FlowEntrySpec`
- `com.ghostlock.app.core.flow.FlowEntryResolution`
- `com.ghostlock.app.core.flow.FlowEntryValidator`
- `com.ghostlock.app.core.flow.FlowEntryRegistry`

原因不是“core 可以随意依赖 Android”，而是 `FlowHostEnv` 已进入反射方法 descriptor。现行入口 descriptor 的参数必须继续是：

`Lcom/ghostlock/app/core/flow/FlowHostEnv;`

把接口移动到 `backend/host` 会在源码看似等价时改变 JVM descriptor，并使 profile/registry 中既有 reflection symbol 的入口签名失配。因此本计划只登记一个有界例外：`FlowHostEnv.context: android.content.Context?` 可留在该 ABI 文件，`core/flow` 其它文件不得新增 Android import。

### A2.2 唯一移动项

`app/src/main/kotlin/com/ghostlock/app/core/flow/FlowHostParams.kt` 拆成：

1. 原路径保留 pure `FlowHostParams`、预算常量与 map 校验；不再 import `android.os.Bundle`。
2. 新建 `app/src/main/kotlin/com/ghostlock/app/backend/host/FlowHostParamsBundleCodec.kt`，只拥有 `Bundle` 编解码。

所有入口、registry、annotation、validator 和 `FlowHostEnv` 均不移动。KC3 的 ABI 门必须比较改前/改后 validator 接受的 exact descriptor、annotation、return type、registry symbol set；不以只编译成功代替。

### A2.3 结构化事件的接入点

KC5 在 **同一个垂直批**把 `FlowHostEnv.log(String)` 改为 `FlowHostEnv.events: LogEmitter`，但 `FlowHostEnv` 的 FQCN 不变。该批同时修改所有入口实现、App host、Shizuku host、launcher callback、`RunCoordinator`，并删除 `FlowProgress`；不允许先删 `FlowProgress` 再放置 raw-string 兼容桥。

## A3. EventDispatcher 冻结合同

### A3.1 常量与状态

public runtime surface 固定为下列语义（实现可以用普通/sealed class，但不得增设第二 lifecycle authority）：

```kotlin
enum class TerminalSignal { Completed, Aborted, Handoff, Destroyed }

sealed interface DrainReport {
    data object Drained : DrainReport
    data class TimedOut(val pendingSinkIds: Set<String>) : DrainReport
}

interface EventSink {
    val sinkId: String
    fun accept(event: LogEvent)
    fun terminal(signal: TerminalSignal, code: Int? = null)
    fun close()
}

class EventDispatcher {
    fun emit(fact: LogFact)
    fun seal(signal: TerminalSignal, code: Int? = null): DrainReport
    fun diagnostics(): EventDispatcherDiagnostics
}
```

`code` 只允许 `Completed` 携带；其它 signal 带非null code属于invalid input。`emit`与`seal`都不返回可供Flow成功/失败分支使用的sink结果。

| 项 | 冻结值 |
|---|---:|
| event schema | `1` |
| 每个 sink queue 容量 | `256` events |
| app 当前 run retained count | `512` events |
| app 当前 run retained bytes | `262144` bytes |
| terminal drain deadline | `1500 ms` |
| component 最大 ASCII bytes | `32` |
| formatted line 最大 UTF-8 bytes | `512` |
| sequence 起点 | `0` |

dispatcher 的正常状态为 `OPEN → SEALED → CLOSED`，每个 sink runtime 另有 `DRAIN_TIMED_OUT_OWNED`：

1. `OPEN`：producer 可 `emit(LogFact)`。
2. `SEALED`：terminal owner 已停止 producer；新 emit 不进入任何 sink，只让 `lateDropCount` 饱和加一。
3. `DRAIN_TIMED_OUT_OWNED`：1500 ms 到期但 worker 未 acknowledgement；queue、worker、callback/fd 与 sink对象继续由原 run scope持有，禁止释放、复用或启动同execution slot的新run。
4. `CLOSED`：**所有** sink worker 已执行 terminal/close 并 acknowledgement；不得重新打开。deadline 本身永远不能把 state 改成 CLOSED。

`emit` 的线性化点由 dispatcher 内单锁保护：校验 → minimum filter → 读取单调时钟 → 分配 sequence → 为每个固定 sink 做一次 non-blocking offer。producer 线程不执行 Binder、文件或 Logcat I/O。

### A3.2 sequence、clock、drop

- 第一个被接受 event 为 sequence 0；`Long.MAX_VALUE` 可被接受一次，此后所有 event 作为 overflow drop，sequence 不环回。
- `elapsedMs=max(previousElapsedMs, monotonicNow-start)`；clock 倒退不产生负值。
- 每个 sink 一条独立 bounded queue 和一个 worker；慢/挂 sink 不能阻塞 producer或其它 sink。
- queue 满时丢弃该 sink 的 **newest incoming nonterminal event**，只增加该 sink 的饱和 `dropCount`；已排队顺序不变。
- validation/filter 发生在编号之前；非法或低于 threshold 的 fact 不消耗 sequence。非法 fact 增加 `invalidDropCount`，threshold filter 增加 `filteredCount`。
- counter 达 `Long.MAX_VALUE` 后保持最大值。
- 任一 `accept`/`close` 异常只终结该 sink worker并登记该 sink failure；不递归 emit，不改变 Flow result/retry/cancel/handoff。
- sink 构造器与接口不得持有 `LogEmitter/EventDispatcher`；diagnostic 只能写饱和 counter，architecture test 拒绝 sink→emitter dependency，从结构上禁止 reentrant log loop。

### A3.3 terminal barrier

terminal owner 的顺序固定为：

1. 使业务 producer 停止；
2. 原子执行 `OPEN → SEALED`；
3. 为每个 sink 排入私有 close sentinel；若 queue 已满，移除队尾 newest nonterminal event并增加 drop，以保证 sentinel 一定进入；
4. worker 按 FIFO 消费 sentinel 之前的 event；callback sink随后在同一worker提交oneway terminal callback；file sink flush/close；worker最后acknowledgement；
5. owner 最多等待 1500 ms；全部ack返回`Drained`，否则返回带exact pending ids的`TimedOut`并将这些runtime标为`DRAIN_TIMED_OUT_OWNED`。超时不改 Flow terminal code，也不取消/interrupt正在执行的Binder/file调用；host仍按launcher authority结束/销毁。

重复 `seal` 不再排sentinel：pending仍在时返回当前`TimedOut`，全部ack后返回`Drained`。第一次seal后的event全部late-drop，不能排在terminal callback后。owner不在dispatcher锁内等待worker，避免worker回调与producer锁互锁。oneway callback的ack只证明本地Binder提交返回，不证明远端UI已消费；terminal authority始终是state/verify。

one-shot App host/Shizuku host可在既有terminal/destroy路径终结进程，进程死亡是仍pending worker的最终OS边界；进程存活期间不得先关其资源。legacy app-local run超时后由main process `PendingDrainRegistry`继续持有runtime，`RunCoordinator`拒绝下一run并返回`observability:drain-pending`，直到ack后移除；不得为继续运行而泄漏旧worker。

App/`:flowhost`/Shizuku 的 Flow terminal authority仍是 `FlowResult + host state/verify`；日志 drain 不是成功判据。callback Binder death 的攻击停止/宿主退出语义仍由 `flow-process-launcher-plan.md` 管理，不能被“sink failure 不改 FlowResult”误读成忽略既有 Binder-death 生命周期。

### A3.4 RunCoordinator retention

- 只保留当前 generation；旧 generation event 直接丢弃并计 stale diagnostic。
- `RunSessionState.terminalItems` 只含两个固定 `Notice` 与 `Event(LogEvent)`；Notice不带level/origin/sequence且不进入sink，因此不是第二日志DTO。
- 同时施加 512 个 Event items 和 256 KiB formatted event bytes 两个上限；两个固定 Notice 不计配额。超过任一上限时从最旧 event 开始淘汰。
- terminal 结果不存成伪 `LogEvent`；终态 state 字段始终保留。
- UI renderer 只读取 `LogEvent`，不重新编号、不改 level/origin/reason。

### A3.5 资源所有权表

| 资源 | 创建者 | 唯一 owner | 借用者/访问区间 | 终结点 | 释放者 |
|---|---|---|---|---|---|
| run dispatcher | App/host composition root | 对应 run scope；timeout后仍是owner | producers 在 OPEN | terminal/abort/handoff/destroy；全worker ack | run scope在ack后释放；one-shot process death为最终边界 |
| sink queue + worker | dispatcher constructor | 单个 sink runtime | dispatcher offer；worker poll/调用sink | close sentinel；1500ms只产生TimedOut | worker ack后由sink runtime释放；timeout不得先释放 |
| callback Binder ref | launcher/host callback registration | host service/UserService | callback sink worker 到 terminal | clear/death/destroy | 既有 host lifecycle owner |
| app process log root fd | main-only `AppContainer` | `ProcessLogFile` instance | process sink；launcher只请求duplicate | app process shutdown | main-only `AppContainer`；`:flowhost`/Shizuku不得创建 |
| current/previous log path | `ProcessLogFile.open` | `AppContainer` startup | 无fd时检查；run中只append current | 下次process startup且current>64MiB | startup owner执行delete-old/atomic rename；失败则禁用sink |
| per-run transferred fd | launcher duplicate/transfer | 接收 host run scope | host file sink 到 terminal/destroy | terminal/destroy/death | host run scope |
| native duplicated fd | 既有 `NativeLogBridge` attach | 对应 DSO logger | native logger 到既有 close | 既有 native close | 对应 DSO；Kotlin不得偷关 |
| retained event list | `RunCoordinator` | current run generation | ViewModel/renderer immutable snapshot | next generation/clear | coordinator |

本计划不改变 native duplicated fd 的 ABI、attach registry 或 close 顺序；表中只要求 Kotlin refactor 不破坏既有 owner。

### A3.6 process role、threshold 与 caller disposition

| role | composition顺序 | 可存在实例 | 禁止实例 |
|---|---|---|---|
| main | role detect → `ProcessLogFile` startup检查/open → `NativeLogBridge.attach` → `ProcessLogController` → repositories/UI | 1 AppContainer、1 finish repository、1 process controller/root fd | FlowHostProcessContainer |
| `:flowhost` | role detect → minimal FlowHostProcessContainer → valid configure/prepare → transferred fd owner → run-scoped`NativeLogBridge.attach` → dispatcher → Flow | 1 minimal host container、至多1 active/pending run dispatcher、run fd的既有native duplicates | AppContainer、finish/settings/backend UI repositories、process root fd、startup attach |
| Shizuku | UserService create → registry snapshot → valid configure → per-run dispatcher/transferred fd | service/run-scoped objects | GhostLockApp/AppContainer/main repositories/root fd |

`ProcessLogController` 用单一 `AtomicReference<LogLevel>`保存一般app日志threshold。settings init/save/observe同步更新它；worker/fd/sequence不重建。每个run只在创建时snapshot一次，host不得读prefs/env/profile。process role与threshold tests使用显式fake factory计数实例，不靠日志推断。

| 当前 caller | KC5 目标 | 注入/保留规则 |
|---|---|---|
| `GhostLockApp.kt` | process emitter | main composition创建后发startup事实；flowhost不发main startup事实 |
| `data/backend/SharedPreferencesBackendConfigStore.kt` | process emitter | constructor注入 |
| `data/backend/BackendSessionStore.kt` | process emitter | constructor/factory注入 |
| `ui/util/AppIconCache.kt` | process emitter | caller显式传入；`AppIconImage.kt`负责从composition取得 |
| `ui/component/markdown/GithubMarkdown.kt`、`MarkdownContent.kt` | process emitter | `LocalProcessLogEmitter`显式提供；default具名失败，不丢弃 |
| `ui/component/dialog/Dialog.kt` | process emitter | 同一CompositionLocal，不建UI logger singleton |
| `data/diagnostics/AndroidDiagnosticReportBuilder.kt` | process emitter | constructor注入；diagnostic失败不递归emit |
| `data/settings/SharedPreferencesOperationalPreferencesAdapter.kt` | threshold controller | 不再configure global logger；只同步调用ProcessLogController update |
| `backend/backends/cve43499/flow/Cve43499RunLog.kt`、`backend/backends/lua/FlowLuaImpl.kt`、`core/flow/SequentialFlow.kt` | run emitter | 每个事实一次emit |
| `data/backend/BackendRunSessionAdapter.kt`、`backend/host/FlowHostService.kt`、`backend/host/HostProcessExec.kt` | 当前run emitter | 不进入process dispatcher第二次写 |
| `shizuku/ShizukuBackendClient.kt` | legacy/current run emitter | legacy分支保留typed run事件；Flow分支由UserService dispatcher编号 |
| `backend/backends/cve43499/request/Cve43499RequestFactory.kt` 的 `GLK-FAILSTOP` | **保留 direct Android Log** | 攻击相邻fail-stop exact whitelist；其它direct Android Log=0 |
| `log/log.kt`、其它global `log` import | 删除 | caller=0、definition=0 |

### A3.7 formatter golden 与极端输入

canonical body：V/D/I恰为`message`；W/E恰为`message + " reason=" + reason`。terminal由origin选择括号且不打印冗余origin token；fd/logcat使用level letter和单字符origin。`terminalMarker`的唯一consumer是terminal formatter。

| fact（elapsed=7、component=`core.test`、origin=Kotlin） | terminal literal | fd literal | logcat tag/body literal |
|---|---|---|---|
| V `scan` | `<V/7> core.test: scan` | `[V/7] origin=K core.test: scan` | `core.test` / `[V/7] origin=K scan` |
| D `branch` | `<~/7> core.test: branch` | `[D/7] origin=K core.test: branch` | `core.test` / `[D/7] origin=K branch` |
| I `ready` | `<*/7> core.test: ready` | `[I/7] origin=K core.test: ready` | `core.test` / `[I/7] origin=K ready` |
| W `retry`, reason=`timeout` | `<-/7> core.test: retry reason=timeout` | `[W/7] origin=K core.test: retry reason=timeout` | `core.test` / `[W/7] origin=K retry reason=timeout` |
| E `failed`, reason=`io` | `<!/7> core.test: failed reason=io` | `[E/7] origin=K core.test: failed reason=io` | `core.test` / `[E/7] origin=K failed reason=io` |

同一Info fact改为Native时terminal=`[*/7] core.test: ready`、fd/logcat origin=`N`；改为Shell时terminal=`(*/7) core.test: ready`、fd/logcat origin=`S`。formatter不从括号反推origin。

| extreme input | 唯一结果 |
|---|---|
| empty message | `invalidDropCount+1`，三sink均0调用 |
| V/D/I带non-null reason | invalid drop；不静默删除reason |
| W/E的reason为null或empty | invalid drop |
| wire任一String为null | adapter boundary drop；不`orEmpty()` |
| 任一rendering恰512 UTF-8 bytes | 接受 |
| terminal/fd/logcat任一rendering为513 bytes | 整个event invalid drop；sink不得各自截断 |
| NUL/CR/LF/non-ASCII/control | invalid drop；不替换、不normalize |

## A4. AIDL transaction ledger

### A4.1 App `:flowhost`

`IFlowHostCallback` 当前隐式 transaction 1/2 在目标中显式冻结：

| id | 目标方法 | 调用型态 | null/失败语义 |
|---:|---|---|---|
| 1 | `onEvent(int version, long sequence, long elapsedMs, int level, int origin, String component, String message, String reason)` | **oneway**；仅 callback sink worker 调用 | AIDL String 全部 non-null；`reason=""` 映射 Kotlin null；本地RemoteException终结callback sink，Flow result不变 |
| 2 | `onTerminal(int code)` | **oneway**；同一worker、排在全部已接受event提交之后 | 仅诊断/FIFO边界；callback失败不改host terminal state/verify |

`IFlowHost` 保持 1..11 与 16777114；仅在尾部追加：

| id | 方法 | 规则 |
|---:|---|---|
| 12 | `int configureObservability(int schemaVersion, int minimumLevel)` | **同步**；只在IDLE、prepare前第一次调用；按下表返回，不fallback |

### A4.2 Shizuku

`IGhostlockCallback` 保持 interface-level `oneway`，因此既有1..4和新增5全部异步：

| id | 方法 | 调用型态 |
|---:|---|---|
| 1 | `onLog(String)` | `oneway`，只给 legacy backend-host |
| 2 | `onStatus(int)` | `oneway`，语义不变 |
| 3 | `onDropped(int)` | `oneway`，语义不变 |
| 4 | `onComplete(int)` | `oneway`，语义不变 |
| 5 | `onFlowEvent(...)` | `oneway`，只由 Flow callback sink worker 调用 |

Flow callback sink执行`terminal()`时只在本地worker形成FIFO/close barrier，不调用legacy `onComplete`；Flow终态仍由`getFlowState/verifyFlow`读取。App `IFlowHostCallback.onTerminal`保留诊断通知是既有两接口不对称的一部分，不为“统一外观”新增Shizuku transaction 6。

`IGhostlockUserService` 保持 1..20 与 16777114；仅在尾部追加：

| id | 方法 | 规则 |
|---:|---|---|
| 21 | `int configureFlowObservability(int schemaVersion, int minimumLevel)` | **同步**；新UserService instance、IDLE、`prepareFlow`前第一次；按下表返回 |

两组service共用exact state/code/reason matrix；code只在`FlowHostProtocol`既有0..6尾部追加，不复用该协议内的旧值：

| 输入/时点 | code | reason | state/config 后置条件 |
|---|---:|---|---|
| IDLE+UNCONFIGURED，schema=1，level=0..4 | 0 | `ok` | Flow state仍IDLE；config变FROZEN |
| schema非1 | 7 | `flow-observability:schema` | 仍UNCONFIGURED |
| level不在0..4 | 8 | `flow-observability:level` | 仍UNCONFIGURED |
| 重复configure或prepare已开始 | 9 | `flow-observability:state` | 原config/state不变 |
| 未configure就prepare/prepareFlow | 10 | `flow-observability:not-configured` | prepare未开始，仍IDLE |

7..10是`FlowHostProtocol`及两个**新增configure方法**的method-local合同；`GhostlockUserService` legacy non-Flow方法现有`GENERIC_ERR_STATE=7`/`GENERIC_ERR_UNSUPPORTED=9`保持原义，不迁移也不与configure结果共用decoder。测试必须按method分别断言，禁止造一个跨所有UserService方法的模糊全局error enum。

prepare业务失败不解冻config；是否可重试仍由原state machine决定。callback rebind/death不重置config。只有新`:flowhost` process或新UserService instance从UNCONFIGURED开始。generated Proxy/Stub测试同时核对callback的`FLAG_ONEWAY`与service配置方法**无**该flag。

### A4.3 threshold authority 与 stale/null 行为

- 持久化 authority 仍是 `SharedPreferencesOperationalPreferencesAdapter` 的 enum `.name`：`Verbose/Debug/Info/Warn/Error`。不改成 lowercase token，不新增第二 key。
- `ProcessLogController` 的一般app threshold由settings init/save/observe同步更新且立即生效；app在run创建时另读取一次preference snapshot，随后用A4.1/A4.2配置host；run中设置变化只影响下一次run。
- host 接受配置后冻结 minimum level；不得从环境变量、Bundle profile 或静态 singleton再读。
- callback 侧校验 version/enum/range/ASCII/length/sequence monotonic；非法 event drop，不投递 UI，不抛回 producer。
- `component/message/reason` 为 null 的恶意/旧 Binder parcel 在 generated Stub 解包或 adapter 边界 fail-closed；不得以 `orEmpty()` 把 null component/message伪装成合法事件。
- generation 已结束、launcher 已解绑或 Binder 来自旧 run 的 event 全部 stale-drop；不得追加到下一 run。
- generated Stub transaction constants 必须由测试核对 exact ids；新增方法只追加，不重排/复用。
- 对未知 transaction，server保持 generated Stub 的 `UNKNOWN_TRANSACTION/false`，client映射具名 transport refusal；不得当成功或回退 raw callback。
- callback Binder death只触发`flow-process-launcher-plan.md`已有的cancel/host teardown owner；event sink不得再发第二次cancel。即使oneway Binder提交或file write卡住，terminal barrier最多等1500ms；超时资源按A3.3继续owned，one-shot host最后由既有destroy/进程终结，不得先关worker仍可访问的对象。

## A5. finish 收敛冻结合同

### A5.1 唯一 domain state

目标唯一持久化模型：

```kotlin
enum class FinishPayloadKind { Root, Command, Lkm }

data class FinishPayloadSlot(
    val id: String,
    val kind: FinishPayloadKind,
    val enabled: Boolean = true,
    val scriptMode: Int = 0,
    val command: String = "",
    val scriptUri: String? = null,
    val lkmUri: String? = null,
    val lkmParams: String = "",
)

data class FinishDefinition(
    val rootManager: String = "KernelSU",
    val safeMode: Boolean = false,
    val payloads: List<FinishPayloadSlot> = listOf(
        FinishPayloadSlot("slot-root", FinishPayloadKind.Root),
    ),
    val restoreSelinux: Boolean = false,
    val kernelAccessAfterRelay: Boolean = false,
    val softRebootAfterFinish: Boolean = false,
)
```

`FinishEditorState` 精确冻结为 `FinishEditorState(val definition: FinishDefinition, val rootManagers: List<FinishRootManagerOption>)`；不可复制definition字段或payload enum/slot。`FinishSettings` 是两字段 projection，不是第二持久化authority。

### A5.2 repository 与 mutation owner

main-only `AppContainer` 创建唯一 `SharedPreferencesFinishRepository` 实例；`:flowhost`/Shizuku不得创建。constructor在repository锁内从prefs读取/decode恰一次，之后`read()`只返回内存snapshot；进程外直接改prefs不支持热同步，只有main process重启才重新读磁盘。它拥有：

- prefs=`ghostlock.finish`、key=`state`；
- v2 encoder 与现有 v1→v2 decoder；
- constructor decode后的内存 snapshot；
- 一把只保护 `read → transform → memory publish → SharedPreferences.apply()` 的锁。

方法仅为 `read(): FinishDefinition` 与 `update(transform: (FinishDefinition) -> FinishDefinition): FinishDefinition`。保持现行语义：内存 snapshot 在 `apply()` 异步落盘前立即可见；不把 `apply()` 伪装成有 commit 回执的事务。

| mutation owner | 允许字段 | 必须原样保留 |
|---|---|---|
| finish editor | `rootManager/safeMode/payloads` | settings 两字段 + `kernelAccessAfterRelay` |
| settings | `restoreSelinux/softRebootAfterFinish` | editor 三字段 + `kernelAccessAfterRelay` |
| 43284 capability | `kernelAccessAfterRelay` | 其它全部字段 |
| preview | 0 个持久化字段 | 只读取传入 snapshot |

| 输入/失败 | 冻结结果 |
|---|---|
| key absent | `FinishDefinition()` 默认值 |
| v1 literal | 按现有 `migrateLegacy` 字段/order规则读成新内存模型；不主动重写，直到下一次真实 mutation |
| v2合法/坏字段 | 保持现有格式不持久化slot id、每次decode重新生成session id的行为；坏数字/Base64/未知kind/短字段继续用`toIntOrNull/default`、空串与unknown-slot skip，不抛到UI |
| 两线程不同字段 update | 单锁按获得锁顺序串行；后者 transform读取前者已发布 snapshot，不丢字段 |
| `SharedPreferences.apply()` 后进程立即死亡 | 进程内已见新snapshot；磁盘是否落盘不伪造保证，重启以后磁盘实际值为准 |
| transform抛异常 | 不发布内存、不调用apply；异常按调用方既有失败边界映射 |
| repository存活期外部prefs被改 | 当前进程snapshot不变；不建listener/第二authority；main process重启后按磁盘实际值重新decode |

### A5.3 file 与 renderer

- 真实执行authority保持`context.filesDir/finish/run.sh`；preview固定为`context.filesDir/finish/preview.sh`。这是对当前“preview覆盖run.sh”竞态的显式修复，不改变真实脚本路径或handoff合同。
- `FinishScriptFileStore`是main-process唯一实例并持有一把file锁。真实prepare在锁内写`run.sh`、重新读exact bytes并构造immutable handoff（path/bytes/digest）后才释放；preview只在锁内写/读`preview.sh`，因此不能覆盖已交付的run snapshot。不得让UI直接写File。
- app-private owner/mode与当前创建方式一致。目录创建失败或write失败抛具名`finish-dir`/`finish-write`，由对应prepare/preview映射失败；不得把旧文件当新成功。`RunCoordinator`既有single-run阻止第二真实prepare并发。
- `run.sh`由执行owner持有：普通成功/失败/取消在finish terminal/finally后best-effort删除；43499 rooted handoff保持到victim执行返回后再删。`preview.sh`在下一preview覆盖、editor clear时best-effort删除。app death残留不作为有效状态；下一次对应write覆盖。delete失败只记process诊断，不改变已经确定的攻击结果。
- `FinishScriptRenderer` 必须对当前 golden 逐字节等价：shebang、`set -u`、LOG/RUN_ID、payload order、manager args、safe mode、SELinux、soft reboot、marker、cleanup均不变。
- A5 触及 43284/43499 finish attack path；对应改后真机门未通过前不得关闭 KC2，也不得把 host golden 当设备成功。

## A6. victim JNI owner 与生命周期

### A6.1 owner 处置

- `core/victim` 保留纯 `VictimNative` port、protocol、session、capability、identity、result 和 `VictimScriptLauncher` orchestration。
- `VictimNativeJni.kt` 移到 `backend/backends/cve43499/victim/Cve43499VictimNativeJni.kt`，因为它调用 `VictimTransportJni`，后者实际 `System.loadLibrary("ghostlock43499")`。
- `Cve43499CommonVictimRuntime`、`Cve43499JniVictimRuntime` 与 43499 factory显式注入该 adapter。
- `VictimScriptLauncher` 删除生产默认 `VictimNativeJni` 依赖，改为构造时必须注入 `sessionFactory`；当前 production caller 为 0，98260 将来必须由自己的 backend composition root 注入 transport，禁止 core 反向找具体 DSO。

### A6.2 token/resource 状态表

| 状态/动作 | token owner | 允许访问 | 终结动作 | 禁止 |
|---|---|---|---|---|
| before spawn | none | validate request | spawn | close/detach nonzero guess |
| spawn not accepted/incomplete/0 | JNI call | result fields only | existing failure mapping；若无 token则无释放 | invent token/kill unknown pid |
| spawn token + invalid pid | adapter | token only | `detachVictim(token)` exactly once | kill/wait unknown pid |
| valid token before adapter return | adapter临时owner | 构造/校验`VictimSpawned` | `VictimResult.Ok`成功返回时转移给caller；返回前异常则`closeVictim(token)` exactly once | 无owner返回/静默泄漏 |
| valid token after adapter return、session publish前 | `VictimSession.start`临时owner | 校验handle/pid、构造`VictimProcess` | 在session锁内publish handle/pid/state时完成转移；此前任何异常/拒绝都`native.close(token)` exactly once | 先publish后构造、双close |
| RUNNING/PARKED | `VictimSession` | serialized send/receive/raw/wait | bye/wait or rooted detach | concurrent access |
| normal close | session | stop access → bye → bounded wait → timeout kill → native close | handle=0/CLOSED | close before access stops |
| rooted detach | session until transfer | best-effort park | set handle=0/DETACHED → native detach exactly once | kill/reap rooted child |
| runtime `newVictimSession()` reentry | old adapter until close completes | no new adapter access before old `closeQuietly` returns | close old → create/publish new adapter | publish new then close old |
| cancel/exception before rooted handoff | current adapter/session | lifecycle stops callers first | existing cleanup/dispose → normal close path | skip close or move detach before rooted proof |
| repeat close/detach | none | no-op | none | reuse consumed token |

KC6 移动adapter owner/注入点，并关闭现有valid-token-untransferred异常窗；其它调用次序、错误reason、timeout、kill policy、park/detach语义、native operation ABI全部不变。移动后的既有`VictimNativeJniTest`增加adapter异常分支的source/owner断言（不增加case）；新增`VO-03`用可注入process factory在session publish前造异常并证明exact close once；invalid pid仍exact detach once。改后立即跑43499真实gate；无设备证据不关闭。

## A7. 批次、文件集与测试账本

### A7.1 批次唯一变量

| 批 | 唯一变量 | production 文件范围 | 同批文档/图 |
|---|---|---|---|
| KC1 | 删除无 production owner 的 18-file island | A1.1 + 3 tests + architecture manifest/checker | introduction、execution architecture、core class diagram |
| KC2 | finish authority垂直迁移（旧模型/store/renderer必须一起退役） | §A7.1.1 KC2 exact paths；不留compat mapper | finish class + resource + sequence三图 |
| KC3 | Bundle codec owner only | `core/flow/FlowHostParams.kt`、新 `backend/host/FlowHostParamsBundleCodec.kt`、host/launcher/UserService codec callers | Flow params IPO/class diagram |
| KC4 | 删除虚假 auto-export UI/无caller tailer | `log/{LogExport,NativeLogTailer}.kt`、settings domain/adapter/ViewModel、`Settings{Screen,Material,Miuix,UiState}.kt`；旧 preference key只忽略不复用 | settings capability table |
| KC5 | raw progress/global logger/重复DTO → structured observability 的单次垂直迁移 | 新 `observability/{contract,runtime}/**`、`data/observability/**`、settings severity、RunSession/renderer、`AppContainer`/`GhostLockApp`/旧log runtime、A2.3、A4 两组 AIDL、所有 Flow entry/host/launcher/UserService/callback caller、`Cve43499RunLog`、`SequentialFlow`、删除 `FlowProgress.kt` | observability class + dispatcher state/resource + Run/Flow sequence/state/wire tables |
| KC6 | victim JNI concrete owner | A6.1 exact files与直接 tests | victim class/resource diagram |
| KC7 | authority final only | machine guards、README 双语、计划指针、review/device-gate index | 只收口，不首次改结构 |

任何结构性批的 `full-process-uml.md` 更新必须与该批代码同批完成；KC7 只检查一致性，不能把所有 UML 延后到末尾。

### A7.1.1 KC2–KC6 exact path ledger

下列清单才是实现边界；上表中的`**`只用于概括package，不授权编辑本清单以外的路径。除已经写出`app/`、`docs/`、`tools/`、Gradle根路径者外，production Kotlin路径均相对`app/src/main/kotlin/com/ghostlock/app/`，unit test路径均相对`app/src/test/kotlin/com/ghostlock/app/`，AIDL路径均相对`app/src/main/aidl/com/ghostlock/app/`；因此每个literal都可还原为唯一repo-relative path。

每条必须带action：`DELETE`删除、`MOVE`保持行为的owner迁移、`MODIFY`原路径编辑、`ADD`新文件。每个计数/结构批还可修改下列共享authority，除此之外发现新路径必须先amendment：

- `MODIFY build.gradle.kts`：唯一`expectedKotlinTests`与root `verifyAllTestXml/checkAll`依赖。
- `MODIFY app/build.gradle.kts`：app XML verifier只消费root expected；不得复制数字。
- `MODIFY tools/kotlin_architecture_d0.py`、`app/src/test/resources/architecture/kotlin-file-actions-v1.tsv`、`app/src/test/kotlin/com/ghostlock/app/architecture/ArchitectureInventoryTest.kt`：对应batch的file/dependency guards。
- `MODIFY docs/development/full-process-uml.md`：对应batch列出的图，必须同批。
- `MODIFY docs/plan/kotlin-core-observability-cleanup-plan.md`、`docs/plan/kotlin-core-observability-cleanup-annex-a.md`、`docs/analysis/architecture-review-log.md`：checkbox、实际路径/结果与评审沿革；不能借此扩production scope。

#### KC2 exact current paths

- `DELETE` Core：`core/finish/FinishConfig.kt`、`core/finish/FinishScript.kt`、`core/finish/FinishScriptStore.kt`、`core/payload/PayloadEffects.kt`、`core/payload/PayloadRequests.kt`；`FinishScript.kt`的行为逐字节迁入下列新增renderer，不保留旧extension authority。
- `MODIFY` Domain：`domain/finish/FinishEditor.kt`、`FinishEditorPort.kt`、`FinishSettings.kt`、`FinishSettingsPort.kt`、`PreviewFinishScriptUseCase.kt`、`ReadFinishEditorUseCase.kt`、`ReadFinishSettingsUseCase.kt`、`SaveFinishEditorUseCase.kt`、`SaveFinishSettingsUseCase.kt`。
- `DELETE` Data：`data/finish/FinishEditorAdapter.kt`、`data/finish/FinishSettingsAdapter.kt`。`MODIFY` composition：`AppContainer.kt`改为唯一repository wiring。
- `MODIFY` 直接production callers：`backend/backends/cve43284/BackendCve43284.kt`、`backend/backends/cve43499/finish/FinishHandoff.kt`、`backend/backends/cve43499/request/Cve43499RequestFactory.kt`。
- `DELETE` test：`ui/screen/module/backends/PayloadEffectsTest.kt`（8 cases）。`MODIFY`且保持原case数：`backend/backends/cve43284/BackendCve43284ConfigurationTest.kt`（5）、`ui/screen/module/backends/FinishScript43284Test.kt`（5）、`ui/screen/module/backends/PayloadRequestsTest.kt`（7）、`ui/screen/module/backends/ScriptRenderTest.kt`（6）；它们改吃唯一model/renderer但不以新golden替代或减少原断言。
- `ADD`：`domain/finish/FinishDefinition.kt`、`domain/finish/RootManagerCatalog.kt`、`data/finish/SharedPreferencesFinishRepository.kt`、`data/finish/AndroidRootManagerProbe.kt`、`data/finish/FinishScriptFileStore.kt`、`core/finish/FinishScriptRenderer.kt`；tests=`data/finish/SharedPreferencesFinishRepositoryTest.kt`（10）与`core/finish/FinishScriptRendererGoldenTest.kt`（8）。

#### KC3 exact paths

- `MODIFY` `core/flow/FlowHostParams.kt`、`backend/host/FlowHostService.kt`、`backend/launcher/AppProcessFlowLauncher.kt`、`backend/launcher/ShizukuProcessFlowLauncher.kt`、`shizuku/GhostlockUserService.kt`、`core/flow/FlowHostParamsTest.kt`。
- `ADD` `backend/host/FlowHostParamsBundleCodec.kt`与`backend/host/FlowHostParamsBundleCodecTest.kt`。
- `core/flow/FlowEntryValidationTest.kt` 只新增 descriptor/FQCN invariant；KC3 不修改 `FlowEntry.kt` production bytes。

#### KC4 exact paths

- `DELETE` `log/LogExport.kt`、`log/NativeLogTailer.kt`。
- `MODIFY` `domain/settings/OperationalPreferences.kt`、`data/settings/SharedPreferencesOperationalPreferencesAdapter.kt`、`ui/viewmodel/SettingsViewModel.kt`、`ui/screen/settings/SettingsUiState.kt`、`SettingsScreen.kt`、`SettingsMaterial.kt`、`SettingsMiuix.kt`、`app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineFixtures.kt`。
- `MODIFY app/src/test/resources/architecture/kotlin-file-actions-v1.tsv`；`ADD app/src/test/kotlin/com/ghostlock/app/data/settings/AutoExportCapabilityRemovalTest.kt`（4）。
- KC4 不得修改其它 logger/Flow/AIDL 文件。

#### KC5 target additions

- `ADD observability/contract/LogLevel.kt`、`LogOrigin.kt`、`LogFact.kt`、`LogEvent.kt`、`LogEmitter.kt`、`LogLineFormatter.kt`。
- `ADD observability/runtime/EventSink.kt`、`EventDispatcher.kt`、`EventDispatcherDiagnostics.kt`、`PendingDrainRegistry.kt`。
- `ADD data/observability/AndroidLogcatEventSink.kt`、`ProcessLogFile.kt`、`ProcessFileEventSink.kt`、`ProcessLogController.kt`。
- `ADD process/AppProcessRole.kt`、`backend/host/FlowHostProcessContainer.kt`、`backend/host/AppFlowCallbackEventSink.kt`、`shizuku/ShizukuFlowCallbackEventSink.kt`、`ui/observability/LocalProcessLogEmitter.kt`。
- `ADD` tests：`observability/contract/LogContractTest.kt`（14）、`observability/runtime/EventDispatcherTest.kt`（16）、`backend/launcher/RunEventRetentionTest.kt`（4）、`ui/screen/run/RunEventRendererParityTest.kt`（4）、`backend/launcher/AppFlowEventWireTest.kt`（8）、`shizuku/ShizukuFlowEventWireTest.kt`（10）。`AW-05..AW-08`覆盖main/flowhost composition、threshold snapshot和direct-Log whitelist，不另增隐形test文件。

#### KC5 current paths modified/deleted

- `DELETE` `log/log.kt`、`log/LogOrigin.kt`、`log/FileSink.kt`、`log/LogFile.kt`、`core/flow/FlowProgress.kt`。
- `MODIFY` composition/runtime：`AppContainer.kt`、`GhostLockApp.kt`、`ui/MainActivity.kt`、`log/NativeLogBridge.kt`（只允许KDoc/caller wiring；external ABI/list/branches不变）、`data/diagnostics/AndroidDiagnosticReportBuilder.kt`。
- `MODIFY` process-emitter callers：`data/backend/SharedPreferencesBackendConfigStore.kt`、`data/backend/BackendSessionStore.kt`、`ui/util/AppIconCache.kt`、`ui/component/AppIconImage.kt`、`ui/component/markdown/GithubMarkdown.kt`、`ui/component/markdown/MarkdownContent.kt`、`ui/component/dialog/Dialog.kt`。
- `MODIFY` settings/run/UI：`domain/settings/OperationalPreferences.kt`、`data/settings/SharedPreferencesOperationalPreferencesAdapter.kt`、`ui/viewmodel/SettingsViewModel.kt`、`domain/run/RunSessionState.kt`、`backend/launcher/RunCoordinator.kt`、`data/backend/BackendRunSessionAdapter.kt`、`ui/viewmodel/RunViewModel.kt`、`ui/screen/run/RunUiState.kt`、`ui/screen/run/RunPage.kt`。
- `MODIFY` Flow contract/producer：`core/flow/FlowEntry.kt`、`core/flow/SequentialFlow.kt`、`backend/backends/cve43499/flow/Cve43499RunLog.kt`、`backend/backends/cve43499/flow/Flow43499Impl.kt`、`backend/backends/cve43499/flow/Flow43499Request.kt`、`backend/backends/cve43499/request/Cve43499RequestFactory.kt`、`backend/backends/lua/FlowLuaImpl.kt`、`backend/backends/debugprobe/FlowDebugProbeEntry.kt`、`backend/diagnostics/DiagnosticFlowEntries.kt`、`backend/host/HostProcessExec.kt`。
- `MODIFY` host/launcher/Shizuku：`backend/host/FlowHostService.kt`、`backend/launcher/AppProcessFlowLauncher.kt`、`backend/launcher/ShizukuProcessFlowLauncher.kt`、`shizuku/GhostlockUserService.kt`、`shizuku/ShizukuFlowHostClient.kt`、`shizuku/ShizukuBackendClient.kt`。
- `MODIFY` AIDL：`app/src/main/aidl/com/ghostlock/app/flow/IFlowHostCallback.aidl`、`app/src/main/aidl/com/ghostlock/app/flow/IFlowHost.aidl`、`app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockCallback.aidl`、`app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockUserService.aidl`。
- `MODIFY` existing contract fakes/tests：`core/flow/FlowEntryValidationTest.kt`、`backend/core/HostProcessExecTest.kt`、`backend/backends/cve43499/flow/Cve43499RunLogTest.kt`、`backend/backends/cve43499glued/Glued43499ExecParamsTest.kt`、`backend/declarations/FlowEntryRegistryAssemblyTest.kt`、`shizuku/ShizukuLauncherSharedTest.kt`。
- architecture manifest/checker 与 exact `full-process-uml.md` 图同批。若实现前 exact caller scan发现本清单外 caller，停止 KC5 并先 amendment，不临时扩写范围。

#### KC6 exact paths

- `MOVE` `core/victim/VictimNativeJni.kt`→`backend/backends/cve43499/victim/Cve43499VictimNativeJni.kt`，unit test `core/victim/VictimNativeJniTest.kt`→`backend/backends/cve43499/victim/Cve43499VictimNativeJniTest.kt`；移动不改变既有test数。
- `MODIFY` production：`core/victim/VictimNative.kt`、`core/victim/VictimSession.kt`、`core/victim/VictimScriptLauncher.kt`、`backend/backends/cve43499/victim/Cve43499CommonVictimRuntime.kt`、`backend/backends/cve43499/victim/Cve43499JniVictim.kt`、`backend/backends/cve43499/config/Cve43499DebugConfig.kt`、`backend/backends/cve43499/request/Cve43499RequestFactory.kt`、`jni/Native43499Jni.kt` KDoc。
- `MODIFY` existing tests：`backend/backends/cve43499/victim/Cve43499JniVictimTest.kt`、`core/victim/VictimSessionTest.kt`、`core/victim/VictimArchitectureTest.kt`。
- `ADD` `backend/backends/cve43499/victim/Cve43499VictimNativeOwnerTest.kt`（7）；七例对应§A6.2的spawn incomplete、bad pid detach、valid token transfer failure、normal close、timeout kill、rooted detach、repeat close。
- 既有`Cve43499JniVictimTest`的`runtime replaces prior session and dispose is idempotent`、rooted detach、close once，以及`VictimSessionTest`的bye/wait/timeout-kill cases必须原名/原断言继续通过；它们不计新增7例，禁止用移动文件掩盖case减少。

### A7.2 unit-test count ledger

current-tree 权威基线来自 D7.3+D7.4-N 已归档 XML：`459 tests / 0 failures / 0 errors / 0 skipped`。本计划不要求“最终仍为459”；root `build.gradle.kts`中的单一`expectedKotlinTests`按下表更新，`:app` verifier只消费它，root `verifyAllTestXml`全量读取repo内`build/test-results/**/*.xml`。任何额外/陈旧XML都进入计数，不得忽略；应清理其producer/任务归属，不得改expected掩盖。

Gradle authority形状冻结为：root extra/property `expectedKotlinTests`保存唯一Int；`:app:verifyDebugUnitTestXml.expectedTests`通过root provider读取，`verifyAllTestXml.expectedTests`读取同一provider。root aggregator显式`dependsOn(":app:verifyDebugUnitTestXml")`，`checkAll`显式依赖root aggregator且不再直接把裸app verifier当最终XML authority。任何subproject脚本出现第二个literal `459/450/.../529`均由architecture/text gate拒绝。

| 批后 | 删除 | 新增 | exact expected tests | 新增测试职责 |
|---|---:|---:|---:|---|
| baseline | 0 | 0 | 459 | D7 当前树 |
| KC1 | 9 | 0 | 450 | dead tests随 owner 一起删除 |
| KC2 | 8 | 18 | 460 | 删除 zero-caller `PayloadEffectsTest` 8；新增 repository 10（v1/v2/坏字段/重复槽/三类 mutation/并发/apply可见性）与 script golden 8 |
| KC3 | 0 | 2 | 462 | descriptor/FQCN保持；Bundle codec值域保持 |
| KC4 | 0 | 4 | 466 | UI capability消失、旧 key ignored、无 MediaStore open、无复生 caller |
| KC5 | 0 | 56 | 522 | contract 14；dispatcher 16；retention/renderer 8；App callback 8；Shizuku callback 10 |
| KC6 | 0 | 7 | 529 | spawn incomplete、bad pid detach、valid-token transfer failure、normal close、timeout kill、rooted detach、repeat close |

测试文件移动或重命名不改变数量；若实现设计需要不同 case 数，必须先 amendment 本表并重新过 critic，禁止在 gate 失败后临时修改 expected 值。

新增case id与文件分组是计数authority的一部分：

| 批 | exact case ids | 数量 |
|---|---|---:|
| KC2 repository | `FR-01..FR-10` | 10 |
| KC2 renderer golden | `FS-01..FS-08` | 8 |
| KC3 params/descriptor | `FP-01..FP-02` | 2 |
| KC4 capability removal | `AE-01..AE-04` | 4 |
| KC5 log contract | `LC-01..LC-14` | 14 |
| KC5 dispatcher | `ED-01..ED-16` | 16 |
| KC5 retention/renderer | `RR-01..RR-04` + `UI-01..UI-04` | 8 |
| KC5 App wire | `AW-01..AW-08` | 8 |
| KC5 Shizuku wire | `SW-01..SW-10` | 10 |
| KC6 victim owner | `VO-01..VO-07` | 7 |

每个新增`@Test`的display name必须以其case id开头；root aggregator只数JUnit XML，不从本表推断通过。最终literal必须为`ALL_TEST_XML=PASS FILES=<actual> TESTS=529 FAILURES=0 ERRORS=0 SKIPPED=0`。

## A8. 明确不进入本计划的技术债

| id | owner | 当前债务/本计划处置 | 量化触发 | 偿还批/前置门 |
|---|---|---|---|---|
| `TD-KC-NATIVE-LOGGER` | `backend/43284` + `common` maintainers | 保留`gl_log`与`df_log`两实现，不做“机械合并” | 43284出现第2个格式/level漂移缺陷，或统一能删除≥1套实现且不增加PI调用 | 独立L设计 + G-43284-real改前/改后门 |
| `TD-KC-NATIVE-REGISTRY` | app CMake + `NativeLogBridge` owner | 保留现有CMake target/artifact manifest；不造第二native-log manifest，不改手写attach | attach名单实际漂移1次或新增第4个attach DSO | 复用现有target manifest的amendment，禁止第二authority |
| `TD-KC-MODULE` | app architecture owner | `app/core`先做package gate，不拆Gradle module | forbidden-edge连续2批为0且出现第2个真实JVM/desktop consumer | module CBN + source-set migration plan |
| `TD-KC-ROTATION` | `data.observability/ProcessLogFile` owner | current+one previous、startup 64MiB、16MiB floor；current run不truncate | 同一设备连续3次run任一次>64MiB，或产品要求>1历史run | 多文件原子归档设计；禁止运行中原地truncate |
| `TD-KC-NATIVE-EVENT` | observability + native owners | native详细行只留现有logcat/file；UI只收Kotlin边界摘要 | 产品明确要求native逐行UI，且给出最大rate/PI禁区验收 | 非PI bounded pipe设计 + 对应设备门 |
| `TD-KC-EXPORT` | settings + `data.observability` owner | 删除当前无caller的虚假auto-export UI/API，不顺手实现新导出 | 用户重新提出导出并给出目录/retention/failure UX | 独立capability/MediaStore设计与端到端验收 |
| `TD-KC-EVENT-V2` | `observability.contract` + flow transport owners | schema v1固定8个primitive字段，App/Shizuku同版本同批迁移 | 新增第9个字段、出现optional集合、第三方/跨版本consumer任一成立 | versioned parcelable/compat矩阵独立wire amendment；两侧Stub id门 |
| `TD-KC-SINK-SCALE` | `observability.runtime` owner | 每run最多3 sink/3 worker/每sink256 queue | 第4个sink、同进程并发run>2、queue实测>1MiB任一成立 | dispatcher executor/内存CBN；不得直接调大queue过门 |
| `TD-KC-FINISH-CONSUMER` | `data.finish` owner | main process单repository + 单进程锁 + run/preview分路径 | 第二进程writer或第二持久化consumer出现 | cross-process serialization/commit authority设计 + 43284/43499双真机门 |
| `TD-KC-FLOWHOST-ABI` | `core.flow` + `backend.host` owners | 仅`FlowHostEnv.context`一个Android例外，FQCN保持 | 需要第2个Android类型或desktop consumer执行FlowEntry | 新version outer env/adapter设计；禁止原地搬FQCN |
| `TD-KC-CALLBACK-ACK` | flow transport owner | callback保持oneway；state/verify是terminal authority，不增加ack transaction | 产品需要remote UI“已渲染”证明或callback成为控制流输入 | 独立wire/timeout/retry设计；不得把sync Binder塞回producer |
