# ADR-0021：typed JNI ABI 与资源所有权（v2 候选）

- 日期：2026-10-23
- 状态：**Proposed；随 JNI v4 一次批准后生效**
- 取代：生效时取代 v1 的“所有函数无状态、无 create/destroy/handle”。
- 权威细节：[JNI v4 §4–§7](../../plan/43499-43284-jni-rewrite-plan.md)。

## 决策

1. **边界**：每 backend 一个 JNI DSO；common 是仅 Kotlin 调用的独立 DSO；backend 之间以及 backend/common 不互调。
2. **调用形态**：值操作无状态；必须跨调用存活且含显式释放资源的对象使用 typed opaque handle。
3. **wire**：具名 request/result，固定宽度 header；单一定义生成 Kotlin/C/manifest。禁止通用 opcode/map/frame。
4. **handle**：`jlong` 是含 DSO/slot/generation/epoch 校验的 ID，不是 pointer。每 DSO 一个固定容量 registry，PI 窗口外使用。
5. **并发**：inFlight pin 防 close UAF；persistent borrow 表达 Actor→Page；仅 clean 解除 borrow，dirty 把完整依赖闭包一并 quarantine；close 先 OPEN→CLOSING，再等 pin=0/borrow=0 后释放。
6. **fork/unload**：atfork child 重建 mutex、增 epoch、清 slots；pre-fork ID stale。应用不主动 unload DSO；OnUnload 仅回收已证明无 pin/borrow 的 clean slot，dirty/unknown slot 记录并钉住至 OS 进程回收。
7. **effect**：返回 clean/dirty/indeterminate；dirty 进入 quarantine，禁止 Cleaner 回收，保留至进程退出，容量耗尽 fail-closed。
8. **PI actor**：三个 mechanism 使用命名 entry，PI 前静态分派；窗口内无 JNI/registry/锁/分配/打印/间接分派。
9. **Kotlin wrapper**：`object` 承载 external；`AutoCloseable class` 隐藏 ID并绑定 RunScopeToken，close 幂等、transfer 后旧对象失效。
10. **Access**：只借用 stable handle 提供 read/write，不拥有 handle，不包装全部 JNI。

## 后果与 CBN

相比全无状态方案，registry 增加一个锁、固定 slots 和 fork/close 协议，但消除 caller 传 fd/mmap/thread owner 导致的跨调用 UAF，并允许 Lua 任意组合仍可统一回收。若 registry 进入 PI 窗口或单 run 资源量超过固定容量×1/2，必须重评 caller-owned arena。详细量化见 plan §5。

## 判据

- 0/1/MAX slot、generation wrap、wrong DSO/kind、stale epoch。
- execute×cancel/close、prepareActor×destroyPage、close×close、fork-while-execute/close。
- Kotlin normal/error/cancel/dispatcher/new-thread/run-after-return。
- `.data/.bss` 仅 manifest registry 符号；PI 窗口引用 registry symbol=0。

## 沿革

v1 无 handle 方向与用户后续“Native 准备资源、句柄交 Kotlin、Kotlin 决定下一步”冲突。v2 保留 Kotlin 编排权威，但把物理资源 owner 留在所属 DSO。用户批准前 v1 仍有效。
