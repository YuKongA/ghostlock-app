# ADR-0022：Flow 为唯一执行权威

- 日期：2026-10-23
- 状态：**Accepted；2026-10-23 Lua/直接能力入口澄清为 Proposed，随 JNI v4 批准后生效**
- 取代：[ADR-0018](0018-execution-authority.md) 的决策 1/2（`PipelineEngine` 唯一、KtStep/Stage 只是 node）；决策 3/4/5（两条 close 路径互斥、能力视图、一次性边界）**继续有效**。
- 关联：[execution-architecture.md](../plan/execution-architecture.md)（其编排章节标历史）、[JNI 计划](../plan/43499-43284-jni-rewrite-plan.md) §5/J2c。

## 背景

2026-10-21 11:54 ADR-0018 定 `PipelineEngine` 为唯一执行权威；同日 14:17–14:43 树里另写了 `core/flow`（`Flow`/`SequentialFlow`，298 行，untracked、无测试、`Flow43499Impl` 为 `not-wired` 占位）。两套编排语义并存：`RunNode/Retryable/Refused/Frozen/skipPolicy` 与 `FlowStep/StepResult/RetryPolicy`。

2026-10-23 用户裁定：**不用 PipelineEngine，用 Flow。**

## 决策

1. **UI run 权威 = `Flow`**：UI 发起的一次 run 只经 `Flow.run(FlowRequest)`；`SequentialFlow` 是默认 backend 模板。该条不把 backend wrapper 或 Access 变成 Flow 私有能力。
2. **每个默认 backend 一个 Flow 实现**（`Flow43499Impl`/`Flow43284Impl`）；其 steps 使用 scope-bound wrapper。`FlowLuaImpl.run` 是 Lua UI run 入口，脚本内部可直接调用任意 scope-bound backend wrapper/Access，不必调用其它 backend 的预制 Flow。
3. **语义**：
   - 失败即停：`StepResult.Failed` 立即结束，返回 `FlowResult.Failed(reason, failedStep, steps, evidence)`。
   - 重试：只重试当前 step；`StepResult.Retry` 按 `RetryPolicy(attempts, delayMs, maxDelayMs, jitterMs)` 退避，耗尽转 `retry-exhausted:<step>:<reason>`。
   - 超时：`StepTiming.timeoutMs`；超时转 `step-timeout:<step>`。
   - 取消：`CancellationException` 原样传播；cleanup 在 `NonCancellable` 中恰好一次。
   - one-shot：`SequentialFlow` 第二次 `run` 抛 `flow is one-shot`。
   - `stop()` 无活动任务时为 no-op；有自有 job 的子类覆盖。
4. **close 权威**：`RunResourceScope`/backend wrapper 是底层 handle 的唯一 owner；`AccessSession`/`AccessView` 只释放自身 borrowed view，不关闭底层 handle。Flow cleanup 只触发 scope 的一次逆序关闭，不与 wrapper 重复 close。
5. **JNI 边界**：值调用无状态；物理资源可由 typed handle 持有。Flow/Lua 只做 Kotlin 编排，route 不进入 Native。
6. **Lua cleanup**：每次 `FlowLuaImpl.run` 建不可伪造的 RunScopeToken；owned factory 无 token fail-closed；finally 在 NonCancellable 中逆序 close。反射自由度不变，支持清单不等于安全边界。

## 后果

- 正面：保留树里已写的 298 行；语义简单（无 Refused/Frozen/skipPolicy）；Lua 等实现可直接实现 `Flow`。
- 代价：`PipelineEngine` 的 §2 语义设计作废；`execution-architecture` 编排章节需标历史；Flow 缺测试与 `Flow43499Impl` 接线。
- 偿还：J2c 补 Flow 的 parity/重试/取消/cleanup 测试，并删除/归档 PipelineEngine 设计。
- 澄清代价：Lua 可组合面大于默认 Flow；以 run scope、typed wrapper、API 兼容清单与泄漏测试偿还，不引入受限 operation registry。

## 沿革（AGENTS §1 强制记录）

| 项 | 内容 |
|---|---|
| 越过条文 | ADR-0018 决策 1/2（Accepted 2026-10-21） |
| 确认人/时间 | 用户 2026-10-23 |
| 代价 | PipelineEngine 设计作废；execution-architecture 编排章节历史化；Flow 需补测试 |
| 偿还 | J2c（Flow parity + 接线）；T-PIPE（归档 PipelineEngine） |

## 批准

| 项 | 内容 |
|---|---|
| 批准人 | 用户（YG） |
| 方式 | 2026-10-23 指令「不用 PipelineEngine，用 flow」 |
| 生效 | 立即；ADR-0018 决策 1/2 同日失效 |

## JNI v4 amendment 批准槽

| 项 | 内容 |
|---|---|
| amendment 状态 | Proposed |
| 批准人/指令/日期 | 待用户 |
| 生效哈希 | 待 D0 冻结 manifest |
