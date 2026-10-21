# 执行架构（execution · 唯一权威）

- 日期：2026-10-21
- 级别：L（跨 Kotlin/native 契约 + 生命周期 + 攻击关键路径）
- **状态**：本文合并并取代三份在办设计——[backend-access-execution-design.md](backend-access-execution-design.md)（rev 7）、
  [unified-execution-engine-plan.md](unified-execution-engine-plan.md)（rev 6）、
  [backends-execution-architecture-plan.md](backends-execution-architecture-plan.md)（rev final）。
  三份**保留为历史**（头部有 superseded 指针），不再各自作为依据；冲突以本文为准。
- 进度仍以 `docs/plan/MASTER-PLAN.md` 为唯一入口；本文只定结构与规则，不记进度。
- 事实基线（路径/文件名）见 `docs/development/introduction.md` §七。

## 1. 三条规则（其余都是推论）

1. **唯一执行权威 = `PipelineEngine`**。顺序、重试、冻结、清理、取消与反向 close 只在这里定义一次。
   `KtStep` 与 `Stage` 只是 **node / backend 形态**，不得各自再实现一遍编排。
2. **`AccessSession` 与 execution 的 close 互斥**：会话只关「能力/JNI 会话句柄」，执行只关「node/端口/Stage 句柄」；
   两条 close 路径**不交叉**，每个句柄**恰好被一方释放一次**。
3. **数据面与执行面分离**：读写走 `AccessSession → AccessCapabilities → KernelMemoryAccess/PageCacheAccess → backend JNI`；
   执行走 `ExecutionEngine → BackendRun → PipelineEngine → node → backend port/JNI`。
   执行节点**不使用**能力接口；能力视图**不参与**执行编排。

~~~mermaid
flowchart TD
  UI["UI / service"] --> EE["ExecutionEngine（一次性）"]
  EE --> BR["BackendRun（每次执行一个）"]
  BR --> PE["PipelineEngine（唯一执行权威）"]
  PE --> N1["Cve43499OpNode（KtStep 形态）"]
  PE --> N2["Cve43284StageNode（Stage 形态）"]
  PE --> CL["cleanup 节点（引擎所有）"]
  N1 --> P1["43499 端口 / JNI"]
  N2 --> P2["43284 Stage / JNI"]
  AS["AccessSession"] --> CAP["AccessCapabilities"]
  CAP --> KM["KernelMemoryAccess"]
  CAP --> PC["PageCacheAccess"]
  KM --> J1["backend JNI（读/写）"]
  PC --> J1
~~~

**箭头即所有权**：实线父=创建者与释放者。`AccessSession` 与 `PipelineEngine` 之间**没有边**——
这正是规则 2 的图形表述。

## 2. PipelineEngine 语义（唯一一份）

```kotlin
data class RunPolicy(val attempts: Int = 1, val settleMs: Long = 0, val skipPolicy: SkipPolicy = SkipPolicy.None)
enum class SkipPolicy { None, FrozenRemaining }

sealed interface RunNodeResult {
    data class Done(val evidence: Map<String, String> = emptyMap()) : RunNodeResult
    data class Retryable(val reason: String) : RunNodeResult
    data class Refused(val reason: String, val evidence: Map<String, String> = emptyMap()) : RunNodeResult
    data class Frozen(val reason: String, val evidence: Map<String, String> = emptyMap()) : RunNodeResult
}

interface RunNode {
    val backendId: String; val key: String; val token: String; val policy: RunPolicy
    suspend fun run(): RunNodeResult
    fun calls(): List<String> = emptyList()
    fun abort(reason: String)
    fun close()
}
```

| 规则 | 内容 |
| --- | --- |
| 顺序 | 按传入列表顺序；每个节点带自己的 `RunPolicy`，**没有全局 attempts**。 |
| 重试 | `Retryable` 只重试**本节点**；次数耗尽 ⇒ `Refused(reason, evidence)`，`calls` 汇总所有尝试，且不再发起 native 调用。 |
| Refused | 立即停止；**无论 skipPolicy 如何都不再产生剩余节点**。 |
| Frozen | 停止，且阻止后续 native 调用；**只有 Frozen 读自己的 skipPolicy**：`FrozenRemaining` 给剩余节点补 `Frozen("engine-frozen")`，`None` 不补。43499 用 `FrozenRemaining`，43284 用 `None`。 |
| 节点抛错（非取消） | 转成 `Frozen(reason, evidence)` + `calls()` 快照；**先 abort 再 cleanup**；abort/cleanup 自身抛错只记录，不替换 primary。 |
| 取消 | `CancellationException` **永不转成结果**：abort 当前节点 → 跑 cleanup → 逆序 close → **原异常原样重抛**；被取消节点不产出 `NodeOutcome`。 |
| cleanup | 引擎拥有的独立 `RunNode`：`NonCancellable finally` 里**恰好一次**，不重试（忽略 policy）、不被 abort；cleanup 非 `Done` ⇒ `overall=Failed`，`primary=Frozen(cleanup)`。 |
| 汇总 | `overall=Ok` 当且仅当所有主节点与 cleanup 全 `Done`；`primary` = 第一个非 `Done` 主节点，cleanup 覆盖它；引擎补的 `engine-frozen` 永不当 primary。 |
| abort | 只针对 `current` 节点，在锁内；abort 的异常记录/打印，不替换 primary。 |
| close | 引擎拥有节点：**逆序、恰好一次、幂等**；**close 后再 run 一律拒绝**；禁止并发 run/close。 |

## 3. 两种 node 形态（不得再长出别的）

### 3.1 `Cve43499OpNode`（KtStep 形态）

- `StepPlan.Missing` ⇒ `Refused("precondition:<token>-<input>")`，`calls=emptyList()`，**不调 `OpsInvoker`**。
- `StepPlan.Ready(emptyList())` ⇒ `Refused("precondition:<token>-no-calls")`，同样不调。
- `StepPlan.Ready`：按序调；ok→`Done`、not-ready→`Retryable`、indeterminate→`Frozen`、failed→`Refused`。
- `abort/close` 为 no-op；`skipPolicy=FrozenRemaining`。
- `Cve43499StepCatalog` + `StepPlans` 只是**翻译器**（token → 有序 `StepSpec`）；它们是数据，不是对象继承。

### 3.2 `Cve43284StageNode`（Stage 形态）

| StageCode | 结果 |
| --- | --- |
| `OK` | `Done` |
| `ENV_MISSING` / `TRIGGER_FAILED` / `WAIT_TIMEOUT` | `Refused` |
| `HELPER_VERIFY` / `MODULE_PARTIAL` / `HOOK_PARTIAL` / `RESTORE_FAILED` | `Frozen` |
| `CANCELLED`（作为返回值） | `Frozen("stage-cancelled")` |
| 抛出 `CancellationException` | **原样传播**，不映射 |

- 映射是**穷尽**的；`StageResult` 的证据原样拷进 `NodeOutcome.result.evidence`。
- `Stage.run()` 保持 `suspend`；`Stage.abort(code)` 与 `Stage.close()` 必须是**非 suspend** 清理原语（否则取消路径无法调用）。
- `abort/close` 委托给 Stage；**只有 PipelineEngine 调它们**。`skipPolicy=None`。
- `StageFactory` 只返回 `StageNodes(nodes, cleanup)` 纯数据；`StagePageCacheEngine` 已删。

## 4. BackendRun / ExecutionEngine 边界

```kotlin
data class BackendRunRequest(val backendId: String, val pathId: String, val values: Map<String, Any>,
    val profile: ByteArray? = null, val expectedUid: Long? = null)
sealed interface BackendRunResult { val steps: List<NodeOutcome>
    data class Ok(val note: String, val uid: Long?, override val steps: List<NodeOutcome>) : BackendRunResult
    data class Failed(val reason: String, override val steps: List<NodeOutcome>) : BackendRunResult }

interface BackendRun : AutoCloseable {
    val backendId: String
    suspend fun run(request: BackendRunRequest, progress: Progress = Progress { _, _, _ -> }): BackendRunResult
    fun stop(); override fun close(); suspend fun shutdown()
}
```

- `NodeOutcome` 只含 `NodeDescriptor / result / calls`，**不含 `RunNode`**；`StepResult`/`StageResult` 是内部证据，不出 BackendRun。
- `BackendRun` **一次性**：一次执行一个；`run` 后再 `run` 拒绝。`stop()` 非 suspend（取消路径可调）；`close()` 非 suspend；`shutdown()` suspend（取消外调用）。
- `DelegatingExecutionEngine`：状态 `Idle → Checking → RunningPath → Finishing → Success/Failed`；
  `close()` 置 `Failed("engine-closed")` 且幂等；`used` 保证一次性；取消时先 `stop()` 再重抛。
- `ExecutionEngineFactory`：校验 key==`backendId`、重复 key 拒绝；每次 `create()` 返回新的**一次性**引擎，工厂本身可复用。
- 取消贯穿全链用**原异常身份**：`StageNode → PipelineEngine → BackendRun → DelegatingExecutionEngine`。

## 5. 访问面（AccessSession · 能力 · 生命周期）

```kotlin
interface AccessSession : NativeSession {          // NativeSession: phase / cleanup / close
    val capabilities: AccessCapabilities
    suspend fun initialize(): AccessResult<InitializationReceipt>
    suspend fun snapshot(): AccessResult<SessionSnapshot>
    suspend fun shutdown(): AccessResult<ShutdownReceipt>
}
interface AccessCapabilities { val kernelMemory: KernelMemoryAccess?; val pageCache: PageCacheAccess? }
```

| 规则 | 内容 |
| --- | --- |
| 能力是视图 | `KernelMemoryAccess` / `PageCacheAccess` 只继承 `AccessCapability` 标记；**非拥有、永不 close**；缺席 = `null`，不给假实现。 |
| 代际 | 视图带 `generation`；会话换代后旧视图一律失效（拒绝，不重试）。 |
| 每 backend 一个会话 | 43284：`Cve43284Session`（pageCache 视图，target 只从 `init.carrierPath` 派生）；LKM：`LkmSession`（kernelMemory 视图）；**43499 没有 AccessSession**——`process_vm_*` 面向用户内存，其 prepare/route/actor 端口归 `Cve43499Run`/`PipelineEngine`。 |
| 组合而非继承 | 未来某 backend 同时具备两种能力 ⇒ 一个会话组合两个视图；**不建 capability 基类**。 |
| 关闭顺序（43284） | `page.shutdown() → ops.tearDown() → jni.close()`，恰好一次，之后 `CLOSED`。 |

### 5.1 close 与 shutdown 的互斥（规则 2 的落地）

~~~mermaid
stateDiagram-v2
  [*] --> INITIALIZING
  INITIALIZING --> READY: initialize ok
  INITIALIZING --> FAILED: initialize 失败
  READY --> CLOSING: close()（取消，不等待、不释放句柄）
  READY --> CLOSING: shutdown() 先 close()
  CLOSING --> CLOSED: shutdown() join 后 nativeShutdown()
  FAILED --> CLOSING: close()
  CLOSED --> CLOSED: 重复 shutdown 幂等（不再 native 清理）
  note right of CLOSING
    close() 只置 CLOSING + 取消 activeJob：
    不 await、不 free native handle、不置 CLOSED
  end note
~~~

- `stateLock: ReentrantLock` 同时守 `phase` 与 `activeJob`（不用 `synchronized`，不设第二把锁）；
  `shutdownMutex: Mutex` 只守 `shutdown()` 自身。
- **准入**：锁内拒绝 `phase != READY`，登记 `activeJob`，解锁，调 native；`finally` 锁内只清自己登记的 job（`phase != CLOSED` 时）。
- **close()**：锁内若已 `CLOSING/CLOSED` 直接返回；否则置 `CLOSING`、取消 `activeJob`、解锁。
- **shutdown()**：已 `CLOSED` 直接回执；非 `CLOSING` 先 `close()`；`shutdownMutex.withLock` 内 join → 置 `activeJob=null`、`phase=CLOSED` → `nativeShutdown()`；重复调用不再 native 清理。
- `DIRTY` **永不**回到 `READY`；`CLOSED` 之后任何能力调用**不得**进 native。

### 5.2 互斥矩阵（谁释放谁，写死）

| 资源 | 所有者 | 释放者 | 谁绝不能释放 |
| --- | --- | --- | --- |
| JNI 会话句柄（43284 / LKM） | `AccessSession` | `shutdown()`（一次） | `PipelineEngine` / `BackendRun` / 节点 |
| 能力视图（`KernelMemoryAccess`/`PageCacheAccess`） | 会话 | 无（非拥有） | 任何人（视图不 close） |
| node / Stage / 端口句柄 | `PipelineEngine` | `PipelineEngine.close()` 逆序一次 | `AccessSession` / UI |
| cleanup 节点 | `PipelineEngine` | 主节点之后 close | 节点自身 |
| 43499 prepare/route/actor 端口 | `Cve43499Run`（经 `BackendRun`） | `BackendRun.close()/shutdown()` | `AccessSession`（本就不存在） |
| 执行引擎（`DelegatingExecutionEngine`） | 调用方（UI） | `close()` 一次 | 工厂 |

## 6. 删除清单与现状

| 项 | 处置 | 现状（2026-10-21 树） |
| --- | --- | --- |
| `core/steps/StepEngine.kt` | 删除（不再是执行权威） | 已删；`core/steps` 只留 `StepId/StepInputs/StepPlans/OpsInvoker` |
| `engine/pagecache/stage/StageRunner.kt` | 删除 | 已删 |
| `engine/pagecache/stage/StagePageCacheEngine.kt` | 删除（Stage 由引擎拥有） | 已删 |
| `core/AccessSession.kt`、`core/AccessSessionFactory.kt` | 并入 `core/NativeSession*` + `core/access/AccessCapability.kt` | 已删；`AccessSession` 现声明在 `core/access/AccessCapability.kt` |
| `data/nativeaccess/JniCapabilitySession(.Factory).kt` | 删除（43284 会话取代） | **仍在树**，待删批次 |
| 通用 read/write JNI | 不建 | 未建 |

## 7. 门禁（本结构的判据）

| 面 | 判据 |
| --- | --- |
| 43499 parity | 顺序 / 重试 / Refused / Frozen / skipPolicy / precondition / Ready(empty) 各有测试。 |
| 43284 parity | 阶段顺序 / 部分成功 / 冻结 / cleanup / restore / 取消 / 无 skipped。 |
| 取消 | 原异常身份贯穿三层；cleanup 恰好一次；close 逆序恰好一次。 |
| 异常 | 节点异常、abort 异常、cleanup 异常、ops 异常 ⇒ 只能是 `Frozen`/`Failed`，不得静默成功。 |
| 一次性 | close 后 run 拒绝、第二次 run 拒绝、close 计数恰好 1。 |
| 会话 | close-then-shutdown、shutdown-then-close、close-in-flight、能力-after-close、`DIRTY` 不回 `READY`、`shutdown` 顺序（page→tearDown→jni.close）。 |
| 结构 | 生产代码里不出现 `StepEngine`/`StageRunner` 符号；`core.steps` 无 backend 分支；节点数据无继承。 |

## 8. 非目标

- 不建通用 read/write JNI；不建 Step/Stage 继承层级。
- 不迁移 43284 JNI 包名/ABI；`ghostlock43284` 仍是外部工件。
- 不在本文写进度、批次或 UI/配置接线（分别属 MASTER-PLAN、kt-step 计划、后端计划）。

## 9. 指针

- 进度：`docs/plan/MASTER-PLAN.md` · 排期：`docs/plan/ROADMAP.md`
- 43499 唯一生产计划：`docs/plan/43499-kt-step-plan.md`；批准状态单表：`docs/plan/43499-approval-status.md`
- 43284：`docs/analysis/43284-refactor-design-v4.md` + `docs/plan/43284-reset-batch.md`
- 会话与能力的语义细化（历史来源，已被本文合并）：三份 superseded 设计
