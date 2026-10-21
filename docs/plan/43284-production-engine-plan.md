# 43284 生产级引擎接入计划（修订版，2026-10-18）

> 2026-10-19：P1/P2/P3 的旧 `KotlinAttackSession`/`Backend43284Port`/`api_43284` 实现段被 `docs/analysis/43284-refactor-design-v4.md` 取代；用户约束与门禁条款保留。

> 子计划：[payload-finish-chain-plan.md](payload-finish-chain-plan.md)（P3/P4/P5 的载荷/收尾单向链、零落盘、marker 去除与 KernelSU 接线）。零落盘约束见 [payload-finish-chain-design.md](../analysis/payload-finish-chain-design.md) v2，与 P4 的通道形态冲突时以子计划为准。

> **2026-10-21 状态**：P1/P2/P3 的实现段已由 `docs/analysis/43284-refactor-design-v4.md` 取代（旧 `KotlinAttackSession` /
> `Backend43284Port` / `api_43284` 已删）；P4/P5/P6 的约束（payload/finish 单向链、零落盘、43499 只写 Kotlin）仍有效。
> **本件被 `payload-finish-chain-plan.md` 与 43499 计划引用，按归档规则原地保留、不移动。**
> 执行结构见 [execution-architecture.md](execution-architecture.md)。

## 0. 目标与决策

**目标**：把 CVE-2026-43284 从临时探针（`TypedBackendProbe` + `tempui`）迁入正式 `ExecutionEngine`，让执行页、开机自动攻击、收尾都走同一套生产路径。

**架构决策（本轮修订）**：
1. **不新增 C++ 统一 Session 抽象**：现有 `session_api` / `api_43284` / `api_43499` 就是会话与步骤的资源所有者。
2. **不新增 `access_api.cpp`**：43284 直接复用 `api_43284` 已有的 `makeAccess` / `accessInitialize` / `writeBlock` / `readBlock`；43499 复用 `makeAccess43499` 与 `writeWord` / `readWord`。
3. **Kotlin 每 backend 一个 Port/Adapter**：`Backend43284Port`、`Backend43499Port`，各自把 native refusal 映射成统一结果；不共享 native 访问句柄。
4. **生命周期统一在 Kotlin 会话**：`KotlinAttackSession` 组合 backend handle + 能力视图 + 步骤端口；能力视图不 close；shutdown 只此一个入口。
5. **RootChannel 独立**：作为 root 执行通道，由 AttackService 拥有，不并入 AccessCapability。

## 1. 现状基线

| 层 | 现状 | 缺口 |
|---|---|---|
| 执行引擎 | `ExecutionEngine` + `ScriptedEngine`（假） | 无生产实现 |
| 步骤模型 | `StepEngine` / `StepPlans` / `PlanCatalog` 已覆盖 43284 五步 | 未接生产 backend |
| 43284 JNI | `api_43284` 已导出 create/initialize/snapshot/resolve/verify/writeBlock/readBlock/makeAccess/hookPlan/trigger/wait/shutdown/destroy | 无 Kotlin 生产适配器 |
| 43499 JNI | `api_43499` 已导出 create/initialize/prepare/discover/spawnVictim/finishVictim/writeWord/readWord/makeAccess/shutdown/destroy | 无 Kotlin 生产适配器 |
| 访问能力 | Kotlin `AccessCapability` 契约 | 43284/43499 适配器未接 |
| 收尾 | `FinishExecution`：RootChannel 优先、su 回退 | root helper 服务端未实现 |
| 执行入口 | `RunCoordinator` 仍调 `TypedBackendProbe.run` | 未接引擎 |
| 原生构建 | `core/CMakeLists.txt` 重构中，缺 `jni/` include | **P0 阻塞** |

## 2. 目标结构

```text
AttackService（:attack）
  └─ ProductionEngine : ExecutionEngine
       ├─ StepEngine（顺序/重试/停止规则）
       ├─ Backend43284Port（Kotlin，调 api_43284）
       ├─ PageCacheAccess（借用 native access 句柄）
       ├─ FinishExecution（RootChannel 优先，su 回退）
       └─ RootChannelClient（socket）

KotlinAttackSession
  ├─ backendHandle（C++ 拥有）
  ├─ capabilities（借用视图，不 close）
  └─ shutdown()：停步骤 → 释放能力 → destroy handle

C++：api_43284（已有）、api_43499（已有）、session_api（已有）
     不新增 Session 类、不新增 access_api.cpp。
```

## 3. 阶段

### P0 基线恢复（阻塞）

- 修复 `core/CMakeLists.txt`（include 与源集合），使完整 `assembleDebug` 可跑。
- 跑通 `./gradlew checkAll :app:testDebugUnitTest`。
- 记录 43284 现有门禁日志作为对照。

**验收**：完整构建成功；无 JNI 符号缺失；设备可安装。

### P1 Kotlin 会话与 Port 契约

- `KotlinAttackSession`：持有 backend handle；`initialize/snapshot/shutdown` 幂等；shutdown 顺序明确；generation 失效旧视图。
- `BackendPort` 接口：`initialize/step/shutdown`，步骤由 `StepEngine` 驱动。
- `Backend43284Port`：封装 create/initialize/snapshot/resolve/verify/writeBlock/readBlock/makeAccess/hookPlan/trigger/wait/shutdown/destroy。
- refusal → `AccessFailure` 单一映射；unknown 写不回成功。

**验收**：fake port 单测覆盖初始化失败、重复 shutdown、拒绝、unknown。

### P2 43284 访问能力适配

- `PageCacheAccess43284`：调用现有 `api_43284` 的 access 入口（makeAccess/accessInitialize/writeBlock/readBlock）。
- descriptor：宽度 16、target/offset 来自 native snapshot；generation 在 restore/rollback 后递增。
- 不新增 `access_api.cpp`；不复制 page-cache 逻辑到 Kotlin。

**验收**：fake native 测试；真机单块写/读回验证；unknown 回执冻结。

### P3 ProductionEngine

- `ProductionEngine : ExecutionEngine`：Idle → Checking → RunningPath → Finishing → Success/Failed。
- 接 `StepEngine` + `PlanCatalog` 的 43284 五步（Patch1/Carrier/Hook/Trigger/Wait）。
- 由 `AttackService` 驱动，不依赖 UI；协作取消；崩溃/中止由进程隔离兜底。
- `RunCoordinator` 切到 ProductionEngine；`TypedBackendProbe` 保留为回滚开关。

**验收**：模拟器完整 Success/Failed；UI 不阻塞；中止杀进程并确认消失。

### P4 收尾与 RootChannel

- `FinishExecution` 已实现 RootChannel 优先、su 回退；补 root helper 服务端契约：
  - socket 用 abstract namespace（不落文件系统）；root helper 由 memfd 执行；
  - 帧协议（GLR1）；
  - peer uid 校验；
  - root helper 由攻击链 root 执行体拉起（LKM umh / root_child）。
- 收尾选项：root 管理器、应用、命令、LKM、SELinux、软重启。

**验收**：有 helper 不走 su；无 helper 回退并记录；退出码回引擎。

### P5 43284 切流与门禁

- 默认走 ProductionEngine；保留开关回退探针。
- 删除 tempui 的生产依赖（测试/回滚入口除外）。
- 真机门禁：writeBlock→readBlock、hook、trigger、wait、收尾、开机自动攻击 + panic 重试。
- 结果归档 `docs/analysis/device-gates/`。

**验收**：门禁全绿；失败具名；回滚开关可用。

### P6 43499 接入（只写 Kotlin）

- `Backend43499Port`：复用现有 `api_43499`，不新增 C++。
- `KernelMemoryAccess43499`：宽度 8、channel PiWindow；descriptor 固定 read/write=8。
- StepEngine 接 W1/W2/W3；route/profile 选择留 Kotlin。
- 设备门禁：固定 CPU 对、冷机、单 route。

**验收**：词写/读回与拒绝路径；不引入 page-cache 假设。

### P7 64560（独立研发轨，非本轮交付）

- 先 PoC 复现与稳定性门禁，不直接进 ProductionEngine。
- 原语、bridge、页通道、分阶段 descriptor 在 C++ 新 backend 内完成。
- 只通过 `Backend64560Port` 接入；默认关闭、独立特性开关、独立进程。
- 达标条件：可重复、可中止、恢复不确定可观测、panic 率可接受。

**验收**：未达门槛前不得标 supported。

## 4. 依赖

```text
P0 → P1 → P2 → P3 → P4 → P5 → P6 → P7
                    └─ P2/P3 可并行（接口先行）
```

## 5. 维护成本影响

| 项 | 43284 | 43499 | 64560 |
|---|---|---|---|
| Kotlin Port | 中 | 小 | 中 |
| C++ 改动 | 无 | 无 | **新建原语 + JNI** |
| 设备验证 | 中 | 时序敏感 | **机型/稳定性重** |
| 长期维护 | 中 | 低–中 | **高** |
| 是否新增 access API | 否 | 否 | 否；由 backend 自己提供 descriptor |

只有出现第三个需要统一 access 句柄的 backend，才重新评估抽取；当前不预造。

## 6. DoD

- [ ] 完整 native 构建 + Kotlin 测试全绿；
- [ ] 43284 五步由 ProductionEngine 执行，RunCoordinator 不再直接调 TypedBackendProbe；
- [ ] 访问能力经既有 backend JNI 提供，生命周期只有 KotlinAttackSession.shutdown 一个入口；
- [ ] 中止 = 杀 :attack 并确认进程消失；
- [ ] 收尾优先 RootChannel，su 回退可用；
- [ ] 开机自动攻击与 panic 重试有设备证据；
- [ ] 门禁归档、回滚开关验证；
- [ ] 43499 仅 Kotlin 接入；64560 独立轨未达门槛不启用。
