# ADR-0018：执行权威唯一化与两条 close 路径互斥

- 日期：2026-10-21
- 状态：**Accepted**（三份来源设计已由 design-critic 通过：rev 7 / rev 6 / rev final，均无阻断项；本 ADR 是合并后的决策记录）
- 取代：无（合并 [backend-access-execution-design](../../plan/backend-access-execution-design.md)、
  [unified-execution-engine-plan](../../plan/unified-execution-engine-plan.md)、
  [backends-execution-architecture-plan](../../plan/backends-execution-architecture-plan.md) 中互相冲突的部分）
- 权威正文：[docs/plan/execution-architecture.md](../../plan/execution-architecture.md)

## 背景

43499（kt-step）与 43284（page-cache）各自长出了执行编排：`StepEngine`（通用顺序/重试/冻结 + `OpsInvoker`）
与 `StageRunner`/`StagePageCacheEngine`（43284 阶段链）。同时存在两份会话契约（`core.AccessSession` 与
`core.access.AccessSession`）与一个过宽的能力标记。结果是：

1. 两个「执行权威」各自定义顺序/重试/冻结，语义漂移（`Refused` 是否继续、cleanup 是否重试、取消如何传播各写一套）；
2. 会话 close 与执行 close 的边界含糊：谁释放 JNI 句柄、谁释放 node/Stage 句柄没有单一答案；
3. 新增 backend 需要同时改 `core.steps` 与 `core.engine`，backend 隔离被破坏。

## 决策

1. **`PipelineEngine` 是唯一执行权威**：顺序、每节点重试、`Refused`/`Frozen` 停机、skipPolicy、cleanup、取消与反向 close
   只在这里定义一次。`StepEngine`/`StageRunner`/`StagePageCacheEngine` 删除。
2. **KtStep 与 Stage 只是 node / backend 形态**：`Cve43499OpNode` 把 `StepPlan` 映射成节点结果；`Cve43284StageNode`
   把 `StageCode` 穷尽映射成节点结果。二者不实现编排，只做「一次节点运行 + abort/close 委托」。
3. **两条 close 路径互斥**：`AccessSession` 只关能力/JNI 会话句柄（`close()` 取消、`shutdown()` join 后 native 清理一次）；
   `PipelineEngine` 只关 node/端口/Stage 句柄（逆序、一次、幂等）。**没有一方调用另一方的 close**。
4. **能力是视图**：`KernelMemoryAccess`/`PageCacheAccess` 非拥有、永不 close、缺席为 `null`、换代即失效；
   43499 不提供 `AccessSession`（`process_vm_*` 面向用户内存，其 prepare/route/actor 端口归 `BackendRun`）。
5. **一次性边界**：`BackendRun` 与 `DelegatingExecutionEngine` 都一次性；`ExecutionEngineFactory` 每次 `create()` 给新引擎。

## 备选与否决理由

| 备选 | 否决理由 |
| --- | --- |
| 保留两个执行权威（Step 与 Stage 各自编排） | 语义漂移不可避免：同一 `Refused` 在两处含义不同；cleanup 重试与取消传播无法统一验收 |
| 抽 `Step`/`Stage` 公共基类，用继承统一 | 步骤是**数据**不是对象；继承会把 backend 差异塞进基类，且引入 PI 热路径不需要的间接分派 |
| 让会话同时拥有执行句柄（一个 close 管全部） | 会话生命周期与执行生命周期长度不同（会话跨多次执行）；合并会导致「执行结束即释放会话」或反之的资源泄漏 |
| 通用 read/write JNI 统一两 backend | 43284 与 43499 的写面机制不同（页缓存 vs PI-futex）；通用 JNI 只会把 backend 差异搬到 native |

## 后果

- **正面**：编排语义只有一份（可测、可验收）；会话与执行的释放责任可逐资源列成矩阵；新增 backend 只写端口与目录表。
- **代价**：节点适配层多一层（`StageNode`/`OpNode`），每次执行多一个对象；`BackendRun` 必须显式 `shutdown()`（`close()` 只取消）。
- **约束**：`Stage.run()` 仍是 `suspend`，但 `abort/close` 必须非 suspend（取消路径要能调用）；
  `CancellationException` 原身份贯穿全链，任何一层都不得把它转成结果。
- **技术债**：`JniCapabilitySession(.Factory)` 仍在树中待删；43284 旧 `access_params.hpp` 按 v4 §2 在 B5 删除。

## 验证

- 43499 parity / 43284 parity / 取消身份 / cleanup 一次 / close 逆序一次 / 一次性拒绝 / 会话 close-shutdown 组合，
  逐项见 [execution-architecture.md](../../plan/execution-architecture.md) §7。
- 未验证项：生产代码中是否仍有 `StepEngine`/`StageRunner` 符号（需一次符号扫描，尚未跑）。
