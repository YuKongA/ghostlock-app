# ADR-0023：AccessCapability 是纯借用视图

- 日期：2026-10-23
- 状态：**Accepted**（用户 2026-10-23 指令：做新的 access capability 权威）
- 取代：[ADR-0018](0018-execution-authority.md) 决策 4 中「视图带 generation、会话换代后旧视图失效」的条款；**非拥有、永不 close、缺席为 null** 继续有效。
- 关联：[execution-architecture.md](../../plan/execution-architecture.md) §5；[access-capability-contract-plan.md](../access-capability-contract-plan.md)（历史契约批次）。

## 背景

旧契约把 `generation: Long` 放在 `AccessCapability` 标记接口上，理由是会话换代后让旧视图失效。但该字段本身不构成防线：

- 接口只暴露一个 `Long`，调用方仍可直接调用 `read/write`；
- 真正的 stale 拒绝必须由持有视图的会话/句柄/native 在调用边界完成；
- 视图是非拥有借用，资源身份与状态已经由 `AccessSession`、typed handle、native registry 持有；再加一个视图代际就是第二权威；
- 当前树没有 `AccessCapability` 实现或调用，generation 只是未落地的旧契约残留。

同时，access 的 read/write request 是纯数据，不在构造器里做业务校验；校验只在导入配置阶段和 native 入口做。

## 决策

1. **`AccessCapability` 只是标记接口**：没有 `generation`、没有 `close`、没有生命周期方法。
2. **所有者唯一**：`AccessSession`（或持有 backend handle 的 `RunResourceScope`）拥有资源并发布 `AccessCapabilities`；能力未就绪时缺席为 `null`，不给假实现。
3. **stale 由所有者/ native 判**：会话关闭、重初始化或 native 状态变化后，所有者不得继续发布旧视图；调用边界由所有者/native 返回具名拒绝。视图不自行检查，也不携带代际 token。
4. **request 是纯数据**：`KernelReadRequest` / `KernelWriteRequest` / `PageCacheReadRequest` / `PageCacheWriteRequest` 不做 `init` 校验；零长度、空字节等组合由导入配置阶段拒绝，native 仍做自己的 ABI/边界校验。
5. **generation 只属于会话观察值**：`InitializationReceipt.generation` / `SessionSnapshot.generation` 可保留为会话元数据；不进入 `AccessCapability`，也不作为写回执的契约字段。
6. **写回执只描述本次写结果**：`AccessWriteReceipt` 只保留 `bytesWritten` 等结果数据，不携带视图代际。

## 前置依赖（2026-10-23 追加）

1. **视图由 owner 在 initialize 成功后构造并发布**：视图实现私有持有 owner/borrow 引用；未就绪时不发布视图（`null`），不构造“假 ready”视图。
2. **request 不携带 handle**：request 只放 address/offset/length/bytes 等操作数据；handle、target 绑定和会话状态属于视图/owner。把 handle 放进 request 会让请求变成可复制、可伪造的第二能力权威。
3. **不在 access 实现里做 lazy initialize**：初始化/绑定属于 `AccessSession`/factory；每次调用只做 owner 状态与 borrow 的准入检查，然后进 native。缺前置条件返回具名 `not-ready` / `stale-view`，不隐式初始化。
4. **泛型不是前置依赖的替代品**：read 与 write 的 request 类型不同，不能用一个 `R` 统一；若需要编译期约束，使用两个类型参数的操作契约（`AccessCapability<ReadReq, WriteReq>`）或保持当前每个子接口精确方法。泛型只约束 request 类型，不约束 owner 生命周期；生命周期仍由 owner 判。

## 获取路径（2026-10-23 追加）

唯一获取路径：

```text
backend 绑定的 AccessSessionFactory
  -> open(): AccessSession（INITIALIZING，无能力）
  -> session.initialize() == Ok
  -> session.capabilities.kernelMemory / pageCache（READY 后才非 null）
  -> 视图 read/write
```

- `AccessSessionFactory` 在构造时绑定 backend/profile/target；`open()` 不接收 handle、不在内部发布能力。
- 调用方不得直接 `new` 视图，也不得自己构造 `AccessCapabilities`；未就绪时缺席用 `null`。
- Flow/Lua 不持有裸 `AccessSession`：`RunResourceScope` 持有 session，在 run 内把视图暴露给 Flow/Lua；`scope.close()` 负责 session 的 shutdown/close，之后旧视图失效。
- 从 factory 到 view 的链路是唯一获权路径；request 只承载每次 read/write 的操作数据。

### backend 初始化在哪里

- `AccessSessionFactory.open()` 只创建 Kotlin session 与 native session 句柄（slot）；**不调用 backend initialize、不 bring_up route、不发布能力**。
- `AccessSession.initialize()` 是 **backend 初始化唯一入口**：用 factory 构造时绑定的 profile/target/route 调 backend/native initialize；成功后 `capabilities` 才非空，失败进 FAILED/CLOSED，不发布半成品视图。
- 43499 没有 `AccessSession`（见 execution-architecture）：它的 init/prepare/discover/route 属于 Flow 的固定步骤，由 `Flow43499Impl` 持有；AccessCapability 只服务 43284 page-cache / LKM kernel-memory 这类数据面。
- Flow/Lua 不自行初始化 backend；它们只使用 owner 已初始化后发布的视图。

## 后果

- 正面：access 层只有一个 owner 和一个 stale 判据；视图不再暗示自带保护；request 保持纯数据。
- 代价：会话实现必须在 close/重初始化时丢弃旧视图引用，并在调用边界做状态检查；这是 owner 的责任，不是接口的。
- 迁移：删除 `AccessCapability.generation`、`AccessWriteReceipt.generation` 与 access request 的 `init` 校验；更新 ADR-0018/execution-architecture 的旧代际表述。

## 验证

- 编译通过 `:app:compileDebugKotlin`（或 Flow/access 子集编译）。
- access request 测试改为纯数据字段保持；不再测试构造器拒绝。
- 代码扫描：`AccessCapability` 无 `generation` / `close`；request 无 `init` 校验。
