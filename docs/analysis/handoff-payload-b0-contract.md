# B0：contract 化设计（backend 实现 contract，route 作为路径）

> 自包含；配套 [plan](handoff-payload-plan.md)、[b1b2](handoff-payload-b1b2-runtime.md)、[ops](handoff-payload-ops.md)、[decisions](handoff-payload-decisions.md)。
> **参数与分派 / 虚接口机制判据**（N9/R2/N14 细节）：[handoff-payload-params-dispatch.md](handoff-payload-params-dispatch.md) §1.1/§4.1/§4.2/§8。基线：工作树未改动。

## 1. 目标

- 让**原语**成为稳定接口：脚本/队列只能经 contract 调 op；**backend 实现写原语**；**route 作为路径**由原语调用；contract 内不出现 route 概念。
- **不破坏**：R1 空账本、PI 窗口无间接分派、**B0.1/B0.2 不动机器码**（B0.3 动机器码）。

## 2. 现状（事实）

| 面 | 现状 |
|---|---|
| identity.hpp | kind/谓词/CombinationSpec/kCombinationCatalog + 概念 BackendIdentity/BackendExecution/BackendState/**TerminalExecution**——**已实现且被两 backend 与两 terminal 实现** |
| step_catalog.hpp:20-21 | **`StepExecution` 是步骤注册点**（`static constexpr StepId` 即满足）；**无生产实现者**（唯一声明在测试，`step_catalog_test.cpp:42-52`） |
| step_plan.hpp | normalize_step_queue **已接线**（glkv3_parse.cpp:315） |
| capability.hpp / capability_adapters.hpp:39/:82 | 能力**掩码定名 = 既有 `contract::CapabilitySet`**（Kind/Error/State 已实现）；**`CapabilityInterface` 是虚基**；适配器已存在但**生产未接线**（`steps.cpp:14` 有 include、0 处使用） |
| `contract::Capabilities`（`core_session.hpp:34`） | **现状 = 5 个虚接口基类指针的非拥有聚合**（非 POD）；**目标态 = 中性 POD**，**改造批次 = B0.2** |

⇒ **结论：注册/能力层一半是死的**（声明齐全、无人接线，且带虚基）。B0 实质 = **把死的接上 + 把虚的挡在 PI 路径之外**。

## 3. 三层契约（修订）

```
    contract::WriteOutcome     中性结果 { code: Ok|Retryable|FallbackSafe|Dirty|Unsupported; step; errno;
                                            userspace_clean; kernel_disarmed }
    contract::WritePrimitive   写原语（backend 实现）：target / mode / leaf / verify
    contract::StepExecution    步骤组装（旧概念，划掉）→ 由 OpSpec / OpRegistry 取代
```

- **contract 只放中性类型**：`WriteOutcome` **不含** route/`RouteStatus`；**route 概念留在 `backend/`**；**取值域封闭** = `Ok | Retryable | FallbackSafe | Dirty | Unsupported`（不得新增第 6 值而不改本表）。
- ~~`contract::WritePath`~~：**不新造**（划掉）。**沿用既有 `RouteLifecycle`**（`route/route_lifecycle.hpp:12-20`：`prepare()→int32_t`、**`execute()` 无参**、`disarm()`、`destroy()`、`status`；`run_route_lifecycle` :24-30 固定顺序）。
- ~~`contract::StepExecution`~~（`step_catalog.hpp:20-21`）：**由 `OpSpec` + `OpRegistry` 取代**（[b1b2](handoff-payload-b1b2-runtime.md) §B1.1）；**fold 归属批次 = B1**。
- **调用方向与分派（R2 统一表述）**：op → `WritePrimitive` → 既有 route（经 `RouteLifecycle`）；**分派在 op 入口、窗口外完成**（条目 route token → `switch` → 编译期策略），**窗口内只执行已选定的策略**。
- **禁令**：虚函数、函数指针、`std::function`；**禁止基类指针虚调用进入 PI 路径**——`contract::Capabilities`（目标态）是中性 POD、不做多态基类；虚基**不得进路径**。钉法：`static_assert(!std::is_polymorphic_v<T>)` + **窗口 TU 的 include 图断言**（由 `op_tu_isolation_test.cpp` 文件级承载，见 [queue-schema](handoff-payload-queue-schema.md) §4）+ `nm` 无 vtable 符号。

## 4. 层归属（R1 防火墙）

| 新件 | 落层 | 允许 | 禁止 |
|---|---|---|---|
| `contract/write_primitive.hpp`（概念 + `WriteOutcome`） | contract | standard + `contract/capability.hpp` | backend/pipeline/platform/terminal |
| 实现（`primitives.*`） | backend | contract/memory/support/race | pipeline |
| 调用点（orchestrator 等） | **pipeline（登记为受限层）** | contract/backend/session/**profile/support/terminal** | **platform** |
| Lua 绑定 / 宿主 API | **`script/`（新建，登记为受限层）** | contract/profile/session/support + `lib/lua` | **backend/platform/terminal** |
| `StepContext`（含 terminal 类型） | backend 或 pipeline | — | **contract** |

- **空账本 `kWhitelist` 保持为空**——不得为本次改动登记豁免；新增越层边（含 stale）即 FAIL；登记落点 = `tests/include_firewall_test.cpp` 层表（B0 同批）。

## 5. 虚接口的处置

- 决定与机制判据**移到** [params-dispatch](handoff-payload-params-dispatch.md) §1.1（决定）与 §4.1（可失败机制），本文件不重复。

## 6. 破坏面

| 类别 | 清单 |
|---|---|
| 生产 | `primitives.{hpp,cpp}`；`steps.{cpp,hpp}` 的 M 能力；`route_policy.hpp`；`route_controller.*`；`route_middleware.*`；`spray.cpp:288-316/682`；`cve_2026_43499_backend.cpp:123-136`；`steps.cpp:14` 死 include；**`contract/step_catalog.hpp`**（StepExecution → OpSpec）；**`contract/capabilities.hpp` + `capability_adapters.hpp:39/:82`**（定名/接线或隔离）；**`core_session.hpp:34`**（→ POD，B0.2）；**`main.cpp:184`**（去重）；**`glkv3_schema.hpp`** / **`profile/schema.hpp`**（bin 放开）；**`glkv3_parse.cpp:151-202`**（R1 物化/越界诊断）、**`document.hpp:55-98`**（`kMaxMapMembers` 12 + `DeclaredArrayField{section,key,members}`）；**`Makefile:94-105`**（Lua vendor） |
| 测试 | `include_firewall_test.cpp`（层表登记 pipeline/script）、`step_catalog_test.cpp:42-52`、`glkv3_schema_test.cpp`、`host/attack_stub.cpp`、`host/backend_dataflow_test.cpp:152`、`route_policy_test.cpp`、`route_controller_test.cpp`、`tcp_zerocopy_route_test.cpp`、`stepset_steps_manifest_test.cpp`、`component_catalog_test.cpp:155-176`、`profile_manifest_v3_test.cpp`（`:165`/`:186` 增 bin 分支） |

## 7. 判据（黑盒/白盒 + 极端值；见 [verification](handoff-payload-verification.md)）

| 判据 | 极端输入 → 预期 | 目标 | 期望 |
|---|---|---|---|
| 层边界 0 边 | 新件 include 禁层 ⇒ FAIL + 空账本 pin | `native-host-tests`（`include_firewall_test`） | **EXIT=0**；非法输入 **≠0** |
| 窗口 TU 禁 include / op TU 隔离 | 窗口 TU 引入禁 include 或具体 route 头 ⇒ 文件级扫描失败；热路径 `nm` 出现 `_ZTV*` ⇒ 失败（TU 清单见 [queue-schema](handoff-payload-queue-schema.md) §4） | **`tests/op_tu_isolation_test.cpp`（新）** | 非法输入 **≠0** |
| gate 判据承载 | 未知 op/缺几何/未声明依赖 ⇒ 具名拒绝 | **`tests/plan_gate_test.cpp`（新）** | 非法输入 **≠0** |
| 概念约束 | 去掉 `static` run/`write` 的类型 ⇒ `static_assert` 失败 | `make -C src ghostlock` | 非法输入 **≠0** |
| 多态禁令 | 路径内引入虚基/基类指针 ⇒ `static_assert` 失败（+ `nm` 无 `_ZTV*`） | `make -C src ghostlock` | 非法输入 **≠0** |
| 无间接调用 | 生成码出现间接 call | `tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock` | 无 indirect call（可选诊断） |
| 真机门禁 | 触碰 `steps`/`primitives` 写路径 | 设备门禁 + `device-gates/*.md` 归档 | **PASS + 日志** |

## 8. 批次

**B0.1** 概念与层归属（新增 `contract/write_primitive.hpp` + 层表登记；**不动机器码**）→ **B0.2** **Capabilities 改目标态 POD** + 接上/隔离 capability（死接口要么接线要么标注隔离；**不动机器码**）→ **B0.3** 条目 route 的编译期选路（**动机器码 ⇒ cmp 归因 + 真机门禁**）→ **B1** 原语注册 + gate（`OpSpec`/`OpRegistry` 取代 `StepExecution`），**B2** Lua 引擎接入。

## 9. 与既有裁决的关系

- 支撑「backend 实现 contract、route 作为路径被原语调用」裁决；`transform` = 未接线能力层**接上或移出攻击路径**。
- **与 b1b2 的分工**：B0 定契约与层归属；B1/B2 定注册表、gate、Lua 运行期；类型定义冲突以 b1b2 为准。
