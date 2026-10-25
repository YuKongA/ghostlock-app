# ADR-0024：43499 在 Flow 与 Native 之间保留 BackendPort 中转层

- 日期：2026-10-23
- 状态：**Accepted**（用户 2026-10-23 指令：Native43499 与 Flow 之间应有一层中转）
- 关联：[43499 production engine plan](../plan/43499-production-engine-plan.md)（Backend43499Port 历史设计）；[AccessCapability 权威](0023-access-capability-authority.md)。

## 背景

43499 的 Flow 是固定步骤编排：会话创建、initialize、prepare、discover、spawnVictim、逐写、victimProbe、finishVictim、close。若 Flow 直接调用 `Native43499`：

- JNI DTO 顺序、LongArray 下标、native CallCode 泄漏到 Flow；
- handle/typed 资源/代际/能力门没有唯一 holder；
- Flow 同时承担编排与 JNI 适配，违反分层；
- 43499 没有 AccessSession（process_vm_* 面向用户内存），不能把初始化塞进 AccessCapability。

## 决策

1. **三层固定**：`Flow43499Impl` → `Cve43499BackendPort` → `Native43499Jni`。
2. **Port 的职责**：持有 native session/handle/typed 资源与状态；执行 create/initialize/prepare/discover/spawnVictim/writeStep/victimProbe/finishVictim/close；映射 native CallCode/refusal；维护 generations 与能力门。
3. **Flow 的职责**：固定步骤顺序、重试、超时、cleanup、请求数据；只认 Port 的具名结果，不直接触 JNI。
4. **Native 的职责**：只接收 typed DTO、执行 syscall/物理机制、返回 native 结果；不知道 Flow、route 组合或 Kotlin 编排。
5. **request 仍是纯数据**：Flow 把 `Flow43499Request` 交给 Port；Port 自己编码 DTO，request 不携带 native handle。
6. **43499 不建 AccessSession**：AccessCapability 只服务 43284 page-cache / LKM kernel-memory 数据面；43499 的初始化/route/actor 由 Port 持有。

## 生命周期

```text
Flow run
  -> Cve43499BackendFactory.open()
  -> Cve43499BackendPort.create()
  -> initialize(request)
  -> prepare(request.prepare)
  -> discover(request.discover, prepareGeneration)
  -> spawnVictim()
  -> writeStep(word) * N
  -> victimProbe(kind)
  -> finishVictim(action, victimGeneration)
  -> shutdown()
```

失败：Port 返回具名 Refused；Flow 停手并走同一 cleanup。shutdown 幂等；Port 不把拒绝折成成功。

## 后果

- 正面：Flow 不再依赖 JNI 细节；Port 是 native 状态/资源的唯一 holder；能力门和代际只有一个判据。
- 成本：多一层接口与适配器；每次 native 调用多一次 Kotlin 转发。
- 未完成：Port 的真实 JNI 实现尚未接线；`Flow43499Impl` 仍返回 not-wired，直到 Port 实现和固定步骤接线完成。

## 验证

- 契约编译；Port 单测（未来）覆盖 create/initialize/prepare/discover/write/probe/finish/shutdown 与拒绝映射。
- 真机门禁仍按 43499 批次执行，不因分层跳过。
