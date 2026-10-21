# 43499 生产级引擎接入计划（K1 · Kotlin 引擎与端口）

> **2026-10-20 新目标（kt-step）**：生产路径改为 [43499-kt-step-plan.md](43499-kt-step-plan.md)（全部逻辑 Kotlin、C++ 仅薄 syscall）。本文件降为 legacy 适配器记录，不再作为生产接线计划；N1b C++ form 路径同样降 legacy。

- 日期：2026-10-20（v3，回应 design-critic C1–C16 与 S5/T4/T5）
- 级别：L（跨 Native/Kotlin 契约 + 生命周期；K1 本身不改 native）
- 关系：本件只覆盖 **K1**。native 设备动作（快照代、form 传播、access 装配、prepare/discover/spawn/finish）见 **[43499-native-device-ops-plan.md](43499-native-device-ops-plan.md)**（草案，待批准，未实现）。
- 用户约束（2026-10-20）：**UI 层执行接线先不做**（`RunCoordinator`、配置声明/获取、profile 绑定均不在本计划）。
- 边界裁定：`43284-production-engine-plan.md` P6 的「43499 只写 Kotlin、不新增 C++」在本计划继续有效；K1 不碰 `core/src`。native 缺口不在本计划内偷偷补，另立草案待批准。
- 明确不做：43284 既有链、64560、Lua/CLI、profile 解析、UI 组件、真机门禁。

> **2026-10-21**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威）；执行结构见 [execution-architecture.md](execution-architecture.md)。
> 本件是 **legacy** 记录（K1 旧适配器），**不接入生产**（MASTER-PLAN §13）。

## 1. 目标与完成定义

把 43499 的**会话/访问调用**接入一个 Kotlin 生产引擎：引擎持有会话生命周期，按 `prepare → discover → spawnVictim → W1/W2/W3 → verify → finish` 驱动；拒绝一律具名且 fail-closed；不依赖 UI、不读配置、不猜默认值。

**K1 完成判据**：给定调用方注入的完整输入，引擎能把 `Backend43499Port` 的具名结果翻译成 `EngineOutcome`；每步失败/不确定/取消都停止并保留证据；所有清理走同一 `finally`；fake port 覆盖 §8 全部用例；真实 native 的缺口由**能力门**具名拒绝，不静默成功。

**K1 不声明**：设备可用、W1/W2/W3 真正落字、uid=0。那些属于 native 计划 + K4 真机门禁。

## 2. 现状基线（2026-10-20，HEAD 92d55d3e；已按评审更正）

| 事实 | 位置 | 结论 |
|---|---|---|
| 43499 已有 20 个类型化 JNI 入口 | `app/src/main/kotlin/com/ghostlock/app/jni/cve43499/Native43499.kt:43-146` | 函数面齐；Kotlin 侧没有生产驱动 |
| `Assembled43499` 无外部调用方 | `.../jni/cve43499/Assembled43499.kt:12-137` | 孤儿；K1 不把它接入（避免第二条读写路径） |
| `StepEngine` 已实现，但仅测试实例化 | `.../core/steps/StepEngine.kt:24-40`；`StepEngineTest.kt` | 规则可复用，不是“已在生产运行” |
| W1/W2/W3 的地址/形状已声明 | `.../core/steps/PlanCatalog.kt:12`；`StepPlans.kt:36-96` | 顺序与 form 1/2/3 已是既有词汇 |
| 43284 生产入口是 `PageCacheEngine` | `.../engine/PageCacheEngine.kt:9-29` | 43499 无对应 Kotlin 入口 |
| UI 对 43499 明确拒绝 | `.../ui/screen/run/RunCoordinator.kt:58-60` | 本计划不改 |
| 设备动作表只接 land/read；五个槽为 null | `core/src/boundary/protocol/api_43499_ops.cpp:59-64` | prepare/discover/spawn/victim_probe/finish 未接线 |
| access 工厂返回未接线对象 | `core/src/backend/cve_2026_43499/access_factory_43499.cpp:18-23` | multicast 为 `NoRouteExec`，initialize 必拒 |
| 快照原 13 字段，**没有三代 generation** | `core/src/boundary/protocol/marshal_43499.hpp:17-22`；`api_43499.hpp:99-101` | K1 用能力门具名拒绝；**K1.1 已补编组**（见 §12） |
| form 没有传到设备写 | `api_43499_ops.cpp:24-37`；`api_43499.hpp:67-75` | 1/2/3 的形状在 typed 路径丢失；K1 不假装可写 |
| 旧 main.cpp 已删除 | `docs/plan/MASTER-PLAN.md:8`（§0.1）；J7.5 待办见 `MASTER-PLAN.md:186`/`§12.2` | 不能回退旧入口 |
| 一个 `MulticastRouteConfig` 固定 target/value | `core/src/backend/cve_2026_43499/landing/multicast_route.hpp:89-130` | 不能用一个 access 连写三步；K1 用**每步一次事务** |

## 3. 目标结构（K1）

```text
KernelMemoryEngine (Kotlin)
  ├─ KernelMemoryRun        调用方输入：会话参数、prepare、discover 工厂、三步计划、expectedUid
  ├─ StepEngine + PlanCatalog(W1/W2/W3) + StepPlans
  ├─ PortOpsInvoker         把一次 write_word 映射成 Backend43499Port.writeStep
  ├─ Backend43499Port       接口：会话生命周期 + 每步一次写事务 + probe/finish
  └─ JniBackend43499Port    唯一 JNI 适配器（Native43499 + 能力门）
```

- **没有 `KernelMemoryAccess` 视图**：现有 `Assembled43499` 与 access 级 `write43499` 已是一条路径，再加视图会成为第三条；能力视图等 native 能真实发布 read/write 描述符时再加（native 计划）。
- **每步一次事务**：K2 的 `writeStep` 路径固定为 `initializeAccess43499(Init43499, route, offset, target=address, value, form, out)`（分配 accessId、构造并初始化 wired access）→ 保存 `out[0]` → **`writeWord`**（会话级，携带 form/三代；不是 `write43499`）→ shutdownAccess(accessId) → destroyAccess(accessId)；不再有独立 makeAccess 步骤，`write43499` 在 K1/K2 零调用（标 legacy）。K1 因 form/target 能力为 false，在触 native 前拒绝。与 `MulticastRouteConfig` 每步 target/value 固定相符，避免长寿命 access 的错误假设。
- **能力门**：`NativeCapabilities(generations, formPropagation, perStepTargetValue)`。`generations` 由 `snapshotFields()` 是否含三代字段判定；`formPropagation`/`perStepTargetValue` 在 K1 的 native 面**没有探针**，一律保守置 false（评审 R5），缺哪项就在触 native 前返回 `unsupported:<name>`，不送 0 代、不猜 form。native 计划再补显式探针。
- **单一读写路径**：K1 只保留 `Backend43499Port`；`Assembled43499` 保持孤儿（native 计划决定 legacy/禁用），不在 K1 接入。

## 4. 数据流与控制流

```text
run(request)
  → create()                          handle > 0
  → initialize(params)                四组 DTO；拒绝 → failed:initialize:<name>
  → prepare(prepare)                  facts.prepared && pageBase != 0
  → discover(discover(prepareGen))    需要 capabilities.generations；否则 unsupported:generation-not-published
  → spawnVictim()                     facts.victimTask != 0
  → StepEngine.run([W1,W2,W3], inputs)
       └─ 每次 write_word → PortOpsInvoker → writeStep(step, address, value, form, offset)
            └─ 需要 capabilities.formPropagation/perStepTargetValue；缺 → unsupported
  → victimProbe(uid) == expectedUid   唯一成功判据
  → finishVictim(action, victimGen)   需要 capabilities.generations
  → close()                           成功/失败/取消同一 finally
```

- **verify**：就是 `victimProbe(0) == expectedUid`，不是独立 StepId；`PlanCatalog` 仍只有 W1/W2/W3。
- **generation**：只从 `Facts43499` 读；K1 的 JNI 适配器在快照缺字段时置 `generations=false`，绝不本地计数。
- **不确定**：native `indeterminate`（或无法判定）→ frozen；引擎不再发调用。
- **取消**：只在阶段边界（prepare/discover/spawn/每步之间）检查；若 `writeStep` 已进入 native，等它返回后再清理；PI 窗口内不可取消。
- **清理顺序**：当前步事务内部保证 shutdown→destroy；引擎级 finish victim（若已 spawn）→ close session。异常与成功同序。

## 5. K1 逐文件改动（全部 Kotlin；不改 `core/src`）

| 文件 | 改动 |
|---|---|
| `app/src/main/kotlin/com/ghostlock/app/engine/kernelmemory/Backend43499Port.kt` | 新增接口与 `PortResult`/`PortRefusal`/`NativeCapabilities` |
| `.../KernelMemoryRun.kt` | 新增 `KernelMemoryRun`、`KernelSession`、`KernelStep`（route/offset/address/target/value）；**assembly 权威 = `Init43499` 单表**，不另建 `StepAssembly` |
| `.../Facts43499.kt` | 快照事实/代/flags 解码；缺字段具名失败 |
| `.../JniBackend43499Port.kt` | `Native43499Api` seam + Android 委托；调用码→具名；能力门；每步事务 |
| `.../PortOpsInvoker.kt` | `OpsInvoker` → `writeStep`；按 address 找 `KernelStep`；保留 form/offset/value |
| `.../KernelMemoryEngine.kt` | 阶段机、StepEngine 驱动、取消/清理、`EngineOutcome` |
| `app/src/test/.../FakeBackend43499Port.kt` | 记录调用顺序与每步参数；可脚本化结果 |
| `app/src/test/.../KernelMemoryEngineTest.kt` | §8 用例 |
| `app/src/test/.../JniBackend43499PortTest.kt` | 能力门、码/拒绝映射、快照解码、每步事务顺序 |
| `app/src/test/.../PortOpsInvokerTest.kt` | address→step、form 1/2/3、重复地址拒绝 |

**不改（K1）**：`Native43499.kt`、`Assembled43499.kt`、`RunCoordinator.kt`、`Backends`/`Profiles43499`、任何 `core/src`。**N2 会扩** `Native43499.kt` 的 external 声明与 `AndroidNative43499Api` 委托，并处置 `Assembled43499` 的 `initialize43499Full` 调用；K1 本批不动。

## 6. 关键选择（CBN；成本含边际成本）

| 决策点 | 备选 | 收益（数字） | 成本/边际 | 翻转条件 | 结论 |
|---|---|---|---|---|---|
| 引擎形态 | A 新 `KernelMemoryEngine`；B 扩 `StagePageCacheEngine` | A 对 43284 改动 0 文件；B 至少改 `StageConfig`/`StageFactory` 2 文件 | A 1 个新文件；B 每加一个 43499 字段要过 43284 门禁 1 次 | 第三个 backend 与 43284 阶段序列完全同形 | A |
| native 边界 | A `Backend43499Port` + 适配器；B 引擎直调 `Native43499` | A 可用 fake 覆盖 10 类用例；B 需要 VM | A +2 文件；B 省 2 文件但每次回归多 1 次设备/VM | 只有一条调用且无清理顺序 | A |
| 步骤编排 | A 复用 `StepEngine`；B 新 43499 序列器 | A 0 新步骤语义，复用 4 条停止规则；B 复制 4 条规则 + 测试 | A 0 新步骤文件；B 至少 1 文件；每新增一步多 1 处 | 43499 需要与 W1/W2/W3 不同的停止规则 | A |
| 访问生命周期 | A 每步一次事务；B 一次 access 连写三步 | A 与 route config 固定 target/value 相符（1 步 1 配置）；B 第 2 步必被 route guard 拒 | A 每步 +2 次 native 调用（make/destroy）；B 少调用但错误 | route 改成支持 per-write target/value 且 guard 解除 | A |
| 能力视图 | A 本批不做；B 加 `KernelMemoryAccess` | A 避免第三条读写路径；B 满足旧 P6 字样 | A 0；B +1 文件 + 双路径对拍 | native 能发布真实 read 描述符且外部有消费者 | A（native 计划再评估） |
| generation 来源 | A 等 native 快照；B Kotlin 本地计数 | A 单一权威；B 0 改动 | A K1 在 discover 前具名拒绝；B 每次运行多 1 个隐藏状态 | native 快照长期不加代 | A（B 需用户强制确认） |

**选定项（A）的边际成本**：新引擎每加一个阶段 +1 分支；port 每加一个 native 函数 +1 适配器方法；StepEngine 复用 0 新步骤；每步事务 +2 次 native 调用（make/destroy）；能力视图不做 0。备选的边际成本已在上表“成本/边际”列。

## 7. 所有权与生命周期（AGENTS §5）

| 对象 | 创建 | 所有者 | 借用者 | 终结点 | 释放者 |
|---|---|---|---|---|---|
| native handle | `JniBackend43499Port.create` | engine | port | `close` | port |
| step access | `writeStep` | port（仅在本次事务内） | — | `shutdown→destroy` | port（try/finally） |
| victim | `spawnVictim` | engine | probe/finish | `finishVictim` | engine |
| facts | native snapshot | engine | steps/verify | 结果对象 | GC |
| StepEngine | engine | engine | — | `run` 返回 | engine |

**不变量**：`writeStep` 成功/失败/取消都必须 shutdown→destroy；access 在 handle 之前释放；finish 在 close 之前；重复 close 幂等；K1 不创建第二个 access 并存。

## 8. 验证矩阵（K1，fake port + fake native API）

| 输入 | 预期 |
|---|---|
| 全成功 | `Succeeded`；uid=expected；顺序 create→init→prepare→discover→spawn→(W1 open/write/close)→(W2…)→(W3…)→probe→finish→close |
| prepare 返回 `op-not-wired:prepare` | `Failed`，reason 原样；后续 native 0 调用；仍 close |
| 快照缺 generation | discover 前 `unsupported:generation-not-published`；不 spawn/不写 |
| form 传播能力=false | 首次 writeStep 前 `unsupported:form-not-propagated`；不 makeAccess |
| 过期代 | `stale-generation:prepare`；不 spawn/不写 |
| W2 写拒绝 | W3 不发出；finish/close 仍执行 |
| `indeterminate` | frozen；W3 不发出；不重试 |
| 取消（阶段边界） | `Cancelled`；已 spawn 则 finish；session close |
| 重复地址 / 非 8 字节块 / 空 route | 拒绝，不触 native |
| `snapshotFields` 缺字段/长度不符 | 具名解码失败，不猜默认 |
| 包内 import | 无 `ui`、无 profile/config |
| 编译/测试 | `:app:compileDebugKotlin` EXIT=0；新增 testsuite 28/28；全量 `:app:testDebugUnitTest` **178 tests / 7 failed / 1 skipped**（失败类见下，非本包） |

**全量口径**：`./gradlew :app:testDebugUnitTest -x exportJniProtocolManifest`（生成器缺失，既有缺口）。XML：tests=178 / failures=7 / errors=0；失败类 `LkmWindow43284Test`(2)、`PayloadRequestsTest`(3)、`ScriptRenderTest`(2)，非本包；K1 只宣称 28/28。

## 9. 回滚与兼容

- K1 只新增文件，不改任何既有文件；回滚 = 删除 `engine/kernelmemory/` 与对应测试。
- 真实 native 缺口由能力门拒绝；K1 合入后 43499 行为仍是“UI 拒绝 + 引擎具名拒绝”，不会变成半成功。
- `Assembled43499` 保持现有状态（孤儿）；K1 不删除、不修改；native 计划再决定 legacy/禁用与恢复清单。

## 10. 明确保留（不做）

- 不改 UI/配置/profile；不改 `RunCoordinator`、`Backends`、`Profiles43499`。
- 不改 43284 的 StageEngine/`StageConfig`/`PageCacheEngine`。
- 不恢复旧 `main.cpp`、Lua、CLI、`TypedBackendProbe`。
- 不新增 C++、不改 native 快照/form/access 装配；这些见 native 草案。
- 不跑真机门禁；不宣称 43499 supported。

## 11. 风险与技术债

| # | 风险/债 | 影响 | 处置/偿还 |
|---|---|---|---|
| R1 | native 快照无三代、form 不传播、access 未装配、五个设备槽为 null | K1 在 discover/write 前具名拒绝；设备仍不可用 | native 草案批准后按 K2/K3 偿还；K1 不掩盖 |
| R2 | 每步一次 access 增加 2 次 native 调用 | 每步多一次 make/destroy 开销 | 与 route config 固定 target/value 的正确性优先；native 计划若支持 per-write，再评估复用 |
| R3 | 取消只在阶段边界生效 | 长 native 调用不可中断 | 记录约束；等调用返回后清理；PI 窗口内不可取消 |
| R4 | `writeStep` 的 native 真实语义在 K1 不可验证 | 只能 fake 证明顺序/拒绝 | native 计划 + K4 真机门禁 |
| R5 | 旧 `Assembled43499` 仍是孤儿 | 读者可能误用 | K1 文档与代码注释标“非生产路径”；native 计划决定处置 |

## 12. DoD（K1）

- [x] 新引擎/端口/适配器/Invoker 编译；fake 测试覆盖 §8；无 UI/profile import；新增 testsuite 28/28 绿（`:app:testDebugUnitTest --tests com.ghostlock.app.engine.kernelmemory.*`；全量 178/7/0 的失败类与 owner 见 §8 与 MASTER-PLAN §13）。
- [x] 能力门：快照缺代/form 不支持时，**不触 native** 且 reason 具名（`JniBackend43499PortTest` 10/10）。
- [x] 生命周期：每步 shutdown→destroy；finish→close；取消/异常同序；重复 close 幂等（`KernelMemoryEngineTest` 13/13）。
- [x] 回滚：删除新增包即可回到 HEAD 行为（本批只新增文件）。
- [x] 计划与 native 草案的指针写入 `MASTER-PLAN.md` §13（同批文档更新，一处权威）。

## 13. K1.1 契约补全（Round 3）

- `marshal_43499.hpp` 增三代 names/fill；`marshal_43499_test` + CMake。
- 证据：`nativeTestHost` 75/75；`nativeBuildAndroid` 绿。
- K1 `generations` 可读 true；form/target 仍 false。N1b C++ form 路径在 kt-step 目标下**降 legacy，不接入生产**；生产路径见 kt-step 计划。

**实现状态（2026-10-20）**：K1 已落地；`writeStep` 在 K1 的能力门下**总是拒绝**（native 面没有 form/target 传输），所以真实 43499 仍停在 `unsupported:form-not-propagated`；这不是设备失败，而是 native 计划 N2 的输入。K1 不声明 43499 supported。
